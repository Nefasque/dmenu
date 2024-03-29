/* See LICENSE file for copyright and license details. */
#include <ctype.h>
#include <locale.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xproto.h>
#include <X11/Xutil.h>
#include <X11/Xresource.h>
#ifdef XINERAMA
#include <X11/extensions/Xinerama.h>
#endif
#include <X11/extensions/Xrender.h>
#include <X11/Xft/Xft.h>

#include "drw.h"
#include "util.h"

/* macros */
#define INTERSECT(x,y,w,h,r)  (MAX(0, MIN((x)+(w),(r).x_org+(r).width)  - MAX((x),(r).x_org)) \
                             * MAX(0, MIN((y)+(h),(r).y_org+(r).height) - MAX((y),(r).y_org)))
#define LENGTH(X)             (sizeof X / sizeof X[0])
#define TEXTW(X)              (drw_fontset_getwidth(drw, (X)) + lrpad)

#define OPAQUE                0xffu

/* enums */
enum {
  SchemeNorm,
  SchemeFade,
  SchemeHighlight,
  SchemeHover,
  SchemeSel,
  SchemeOut,
  SchemeGreen,
  SchemeYellow,
  SchemeBlue,
  SchemePurple,
  SchemeRed,
  SchemeLast
}; /* color schemes */

struct item {
	char *text;
	struct item *left, *right;
	int out;
	int index;
	double distance;
};

typedef struct {
	KeySym ksym;
	unsigned int state;
} Key;

static char text[BUFSIZ] = "";
static char *embed;
static int bh, mw, mh;
static int dmx = 0; /* put dmenu at this x offset */
static int dmy = 0; /* put dmenu at this y offset (measured from the bottom if topbar is 0) */
static unsigned int dmw = 0; /* make dmenu this wide */
static int inputw = 0, promptw, passwd = 0;
static int lrpad; /* sum of left and right padding */
static size_t cursor;
static struct item *items = NULL;
static struct item *matches, *matchend;
static struct item *prev, *curr, *next, *sel;
static int mon = -1, screen;
static unsigned int max_lines = 0;
static unsigned int using_vi_mode = 0;
static int print_index = 0;
static int use_text_input = 0;

static Atom clip, utf8;
static Display *dpy;
static Window root, parentwin, win;
static XIC xic;

static Drw *drw;
static Clr *scheme[SchemeLast];

static int useargb = 0;
static Visual *visual;
static int depth;
static Colormap cmap;

/* Xresources preferences */
enum resource_type {
	STRING = 0,
	INTEGER = 1,
	FLOAT = 2
};
typedef struct {
	char *name;
	enum resource_type type;
	void *dst;
} ResourcePref;

static void load_xresources(void);
static void resource_load(XrmDatabase db, char *name, enum resource_type rtype, void *dst);

#include "config.h"

static int (*fstrncmp)(const char *, const char *, size_t) = strncmp;
static char *(*fstrstr)(const char *, const char *) = strstr;
static void xinitvisual();

static unsigned int
textw_clamp(const char *str, unsigned int n)
{
	unsigned int w = drw_fontset_getwidth_clamp(drw, str, n) + lrpad;
	return MIN(w, n);
}

static void
appenditem(struct item *item, struct item **list, struct item **last)
{
	if (*last)
		(*last)->right = item;
	else
		*list = item;

	item->left = *last;
	item->right = NULL;
	*last = item;
}

static void
calcoffsets(void)
{
	int i, n;

	if (lines > 0)
		n = lines * bh;
	else
		n = mw - (promptw + inputw + TEXTW(symbol_1) + TEXTW(symbol_2));
	/* calculate which items will begin the next page and previous page */
	for (i = 0, next = curr; next; next = next->right)
		if ((i += (lines > 0) ? bh : textw_clamp(next->text, n)) > n)
			break;
	for (i = 0, prev = curr; prev && prev->left; prev = prev->left)
		if ((i += (lines > 0) ? bh : textw_clamp(prev->left->text, n)) > n)
			break;
}

static void
cleanup(void)
{
	size_t i;

	XUngrabKey(dpy, AnyKey, AnyModifier, root);
	for (i = 0; i < SchemeLast; i++)
		free(scheme[i]);
	for (i = 0; items && items[i].text; ++i)
		free(items[i].text);
	free(items);
	drw_free(drw);
	XSync(dpy, False);
	XCloseDisplay(dpy);
}

static char *
cistrstr(const char *h, const char *n)
{
	size_t i;

	if (!n[0])
		return (char *)h;

	for (; *h; ++h) {
		for (i = 0; n[i] && tolower((unsigned char)n[i]) ==
		            tolower((unsigned char)h[i]); ++i)
			;
		if (n[i] == '\0')
			return (char *)h;
	}
	return NULL;
}

static int
drawitem(struct item *item, int x, int y, int w) {
  int iscomment = 0;
  if (item->text[0] == '>') {
    if (item->text[1] == '>') {
      iscomment = 3;
      switch (item->text[2]) {
      case 'r':
        drw_setscheme(drw, scheme[SchemeRed]);
        break;
      case 'g':
        drw_setscheme(drw, scheme[SchemeGreen]);
        break;
      case 'y':
        drw_setscheme(drw, scheme[SchemeYellow]);
        break;
      case 'b':
        drw_setscheme(drw, scheme[SchemeBlue]);
        break;
      case 'p':
        drw_setscheme(drw, scheme[SchemePurple]);
        break;
      case 'h':
        drw_setscheme(drw, scheme[SchemeHighlight]);
        break;
      case 's':
        drw_setscheme(drw, scheme[SchemeSel]);
        break;
      default:
        iscomment = 1;
        drw_setscheme(drw, scheme[SchemeNorm]);
        break;
      }
    } else {
      drw_setscheme(drw, scheme[SchemeNorm]);
      iscomment = 1;
    }

  } else if (item->text[0] == ':') {
    iscomment = 2;
    if (item == sel) {
      switch (item->text[1]) {
      case 'r':
        drw_setscheme(drw, scheme[SchemeRed]);
        break;
      case 'g':
        drw_setscheme(drw, scheme[SchemeGreen]);
        break;
      case 'y':
        drw_setscheme(drw, scheme[SchemeYellow]);
        break;
      case 'b':
        drw_setscheme(drw, scheme[SchemeBlue]);
        break;
      case 'p':
        drw_setscheme(drw, scheme[SchemePurple]);
        break;
      case 'h':
        drw_setscheme(drw, scheme[SchemeHighlight]);
        break;
      case 's':
        drw_setscheme(drw, scheme[SchemeSel]);
        break;
      default:
        drw_setscheme(drw, scheme[SchemeSel]);
        iscomment = 0;
        break;
      }
    } else {
      drw_setscheme(drw, scheme[SchemeNorm]);
    }
  } else {
    if (item == sel)
      drw_setscheme(drw, scheme[SchemeSel]);
    else if (item->out)
      drw_setscheme(drw, scheme[SchemeOut]);
    else
      drw_setscheme(drw, scheme[SchemeNorm]);
  }

  int temppadding;
  temppadding = 0;
  if (iscomment == 2) {
    if (item->text[2] == ' ') {
      temppadding = drw->fonts->h * 3;
      animated = 1;
      char dest[1000];
      strcpy(dest, item->text);
      dest[6] = '\0';
      drw_text(drw, x, y, temppadding, lineheight, temppadding / 2.6, dest + 3, 0);
      iscomment = 6;
      drw_setscheme(drw, sel == item ? scheme[SchemeHover] : scheme[SchemeNorm]);
    }
  }

  char *output;
  if (commented) {
    static char onestr[2];
    onestr[0] = item->text[0];
    onestr[1] = '\0';
    output = onestr;
  } else {
    output = item->text;
  }

  if (item == sel)
    sely = y;
  return drw_text(
      drw, x + ((iscomment == 6) ? temppadding : 0), y,
      commented ? bh : (w - ((iscomment == 6) ? temppadding : 0)), bh,
      commented ? (bh - drw_fontset_getwidth(drw, (output))) / 2 : lrpad / 2,
      output + iscomment, 0);
}

static void
drawmenu(void)
{
	unsigned int curpos;
	struct item *item;
	int x = 0, y = 0, fh = drw->fonts->h, w;
	char *censort;

	drw_setscheme(drw, scheme[SchemeNorm]);
	drw_rect(drw, 0, 0, mw, mh, 1, 1);

	if (prompt && *prompt) {
		if (colorprompt)
			drw_setscheme(drw, scheme[SchemeSel]);
		x = drw_text(drw, x, 0, promptw, bh, lrpad / 2, prompt, 0);
	}
	/* draw input field */
	w = (lines > 0 || !matches) ? mw - x : inputw;
	drw_setscheme(drw, scheme[SchemeNorm]);
	if (passwd) {
	        censort = ecalloc(1, sizeof(text));
		memset(censort, 'X', strlen(text));
		drw_text(drw, x, 0, w, bh, lrpad / 2, censort, 0);
		free(censort);
	} else drw_text(drw, x, 0, w, bh, lrpad / 2, text, 0);

	curpos = TEXTW(text) - TEXTW(&text[cursor]);
	curpos += lrpad / 2 - 1;
	if (using_vi_mode && text[0] != '\0') {
		drw_setscheme(drw, scheme[SchemePurple]); 
		char vi_char[] = {text[cursor], '\0'};
		drw_text(drw, x + curpos, 0, TEXTW(vi_char) - lrpad, bh, 0, vi_char, 0);
	} else if (using_vi_mode) {
		drw_setscheme(drw, scheme[SchemeNorm]);
		drw_rect(drw, x + curpos, 2, lrpad / 2, bh - 4, 1, 0);
	} else if (curpos < w) {
		drw_setscheme(drw, scheme[SchemeNorm]);
		drw_rect(drw, x + curpos, 2 + (bh - fh) / 2, 2, fh - 4, 1, 0);
	}

	if (lines > 0) {
		/* draw vertical list */
		for (item = curr; item != next; item = item->right)
			// drawitem(item, 0, y += bh, mw - x);
			drawitem(item, x - promptw, y += bh, mw);
	} else if (matches) {
		/* draw horizontal list */
		x += inputw;
		w = TEXTW(symbol_1);
		if (curr->left) {
			drw_setscheme(drw, scheme[SchemeNorm]);
			drw_text(drw, x, 0, w, bh, lrpad / 2, symbol_1, 0);
		}
		x += w;
		for (item = curr; item != next; item = item->right)
			// x = drawitem(item, x, 0, textw_clamp(item->text, mw - x - TEXTW(">")));
			x = drawitem(item, x, 0, MIN(TEXTW(item->text), mw - x - TEXTW(symbol_2)));
		if (next) {
			w = TEXTW(symbol_2);
			drw_setscheme(drw, scheme[SchemeNorm]);
			drw_text(drw, mw - w, 0, w, bh, lrpad / 2, symbol_2, 0);
		}
	}
	drw_map(drw, win, 0, 0, mw, mh);
}

static void
grabfocus(void)
{
	struct timespec ts = { .tv_sec = 0, .tv_nsec = 10000000  };
	Window focuswin;
	int i, revertwin;

	for (i = 0; i < 100; ++i) {
		XGetInputFocus(dpy, &focuswin, &revertwin);
		if (focuswin == win)
			return;
		XSetInputFocus(dpy, win, RevertToParent, CurrentTime);
		nanosleep(&ts, NULL);
	}
	die("cannot grab focus");
}

static void
grabkeyboard(void)
{
	struct timespec ts = { .tv_sec = 0, .tv_nsec = 1000000  };
	int i;

	if (embed)
		return;
	/* try to grab keyboard, we may have to wait for another process to ungrab */
	for (i = 0; i < 1000; i++) {
		if (XGrabKeyboard(dpy, DefaultRootWindow(dpy), True, GrabModeAsync,
		                  GrabModeAsync, CurrentTime) == GrabSuccess)
			return;
		nanosleep(&ts, NULL);
	}
	die("cannot grab keyboard");
}

static void readstdin(FILE* stream);

static void
refreshoptions()
{
	int dynlen = strlen(dynamic);
	int cmdlen = dynlen + 4;
	char *cmd;
	char *c;
	char *t = text;
	while (*t)
		cmdlen += *t++ == '\'' ? 4 : 1;
	cmd = malloc(cmdlen);
	if (cmd == NULL)
		die("cannot malloc %u bytes:", cmdlen);
	strcpy(cmd, dynamic);
	t = text;
	c = cmd + dynlen;
	*(c++) = ' ';
	*(c++) = '\'';
	while (*t) {
		// prefix ' with '\'
		if (*t == '\'') {
			*(c++) = '\'';
			*(c++) = '\\';
			*(c++) = '\'';
		}
		*(c++) = *(t++);
	}
	*(c++) = '\'';
	*(c++) = 0;
	FILE *stream = popen(cmd, "r");
	if (!stream)
		die("could not popen dynamic command (%s):", cmd);
	readstdin(stream);
	int r = pclose(stream);
	if (r == -1)
		die("could not pclose dynamic command");
	free(cmd);
}

int
compare_distance(const void *a, const void *b)
{
	struct item *da = *(struct item **) a;
	struct item *db = *(struct item **) b;

	if (!db)
		return 1;
	if (!da)
		return -1;

	return da->distance == db->distance ? 0 : da->distance < db->distance ? -1 : 1;
}

void
fuzzymatch(void)
{
	/* bang - we have so much memory */
	struct item *it;
	struct item **fuzzymatches = NULL;
	char c;
	int number_of_matches = 0, i, pidx, sidx, eidx;
	int text_len = strlen(text), itext_len;

	matches = matchend = NULL;

	/* walk through all items */
	for (it = items; it && it->text; it++) {
		if (text_len) {
			itext_len = strlen(it->text);
			pidx = 0; /* pointer */
			sidx = eidx = -1; /* start of match, end of match */
			/* walk through item text */
			for (i = 0; i < itext_len && (c = it->text[i]); i++) {
				/* fuzzy match pattern */
				if (!fstrncmp(&text[pidx], &c, 1)) {
					if(sidx == -1)
						sidx = i;
					pidx++;
					if (pidx == text_len) {
						eidx = i;
						break;
					}
				}
			}
			/* build list of matches */
			if (eidx != -1) {
				/* compute distance */
				/* add penalty if match starts late (log(sidx+2))
				 * add penalty for long a match without many matching characters */
				it->distance = log(sidx + 2) + (double)(eidx - sidx - text_len);
				/* fprintf(stderr, "distance %s %f\n", it->text, it->distance); */
				appenditem(it, &matches, &matchend);
				number_of_matches++;
			}
		} else {
			appenditem(it, &matches, &matchend);
		}
	}

	if (number_of_matches) {
		/* initialize array with matches */
		if (!(fuzzymatches = realloc(fuzzymatches, number_of_matches * sizeof(struct item*))))
			die("cannot realloc %u bytes:", number_of_matches * sizeof(struct item*));
		for (i = 0, it = matches; it && i < number_of_matches; i++, it = it->right) {
			fuzzymatches[i] = it;
		}
		/* sort matches according to distance */
		qsort(fuzzymatches, number_of_matches, sizeof(struct item*), compare_distance);
		/* rebuild list of matches */
		matches = matchend = NULL;
		for (i = 0, it = fuzzymatches[i];  i < number_of_matches && it && \
				it->text; i++, it = fuzzymatches[i]) {
			appenditem(it, &matches, &matchend);
		}
		free(fuzzymatches);
	}
	curr = sel = matches;
	calcoffsets();
}

static void
match(void)
{
	if (fuzzy) {
		fuzzymatch();
		return;
	}
	static char **tokv = NULL;
	static int tokn = 0;

	char buf[sizeof text], *s;
	int i, tokc = 0;
	size_t len, textsize;
	struct item *item, *lprefix, *lsubstr, *prefixend, *substrend;

	if (dynamic) {
		refreshoptions();
		matches = matchend = NULL;
		for (item = items; item && item->text; item++)
			appenditem(item, &matches, &matchend);
		curr = sel = matches;
		calcoffsets();
		return;
	}

	strcpy(buf, text);
	/* separate input text into tokens to be matched individually */
	for (s = strtok(buf, " "); s; tokv[tokc - 1] = s, s = strtok(NULL, " "))
		if (++tokc > tokn && !(tokv = realloc(tokv, ++tokn * sizeof *tokv)))
			die("cannot realloc %zu bytes:", tokn * sizeof *tokv);
	len = tokc ? strlen(tokv[0]) : 0;

	matches = lprefix = lsubstr = matchend = prefixend = substrend = NULL;
	textsize = strlen(text) + 1;
	for (item = items; item && item->text; item++) {
		for (i = 0; i < tokc; i++)
			if (!fstrstr(item->text, tokv[i]))
				break;
		if (i != tokc) /* not all tokens match */
			continue;
		/* exact matches go first, then prefixes, then substrings */
		if (!tokc || !fstrncmp(text, item->text, textsize))
			appenditem(item, &matches, &matchend);
		else if (!fstrncmp(tokv[0], item->text, len))
			appenditem(item, &lprefix, &prefixend);
		else
			appenditem(item, &lsubstr, &substrend);
	}
	if (lprefix) {
		if (matches) {
			matchend->right = lprefix;
			lprefix->left = matchend;
		} else
			matches = lprefix;
		matchend = prefixend;
	}
	if (lsubstr) {
		if (matches) {
			matchend->right = lsubstr;
			lsubstr->left = matchend;
		} else
			matches = lsubstr;
		matchend = substrend;
	}
	curr = sel = matches;
	calcoffsets();
}

static void
insert(const char *str, ssize_t n)
{
	if (strlen(text) + n > sizeof text - 1)
		return;
	/* move existing text out of the way, insert new text, and update cursor */
	memmove(&text[cursor + n], &text[cursor], sizeof text - cursor - MAX(n, 0));
	if (n > 0)
		memcpy(&text[cursor], str, n);
	cursor += n;
	match();
}

static size_t
nextrune(int inc)
{
	ssize_t n;

	/* return location of next utf8 rune in the given direction (+1 or -1) */
	for (n = cursor + inc; n + inc >= 0 && (text[n] & 0xc0) == 0x80; n += inc)
		;
	return n;
}

static void
movewordedge(int dir)
{
	if (dir < 0) { /* move cursor to the start of the word*/
		while (cursor > 0 && strchr(worddelimiters, text[nextrune(-1)]))
			cursor = nextrune(-1);
		while (cursor > 0 && !strchr(worddelimiters, text[nextrune(-1)]))
			cursor = nextrune(-1);
	} else { /* move cursor to the end of the word */
		while (text[cursor] && strchr(worddelimiters, text[cursor]))
			cursor = nextrune(+1);
		while (text[cursor] && !strchr(worddelimiters, text[cursor]))
			cursor = nextrune(+1);
	}
}

static void
vi_keypress(KeySym ksym, const XKeyEvent *ev)
{
	static const size_t quit_len = LENGTH(quit_keys);
	if (ev->state & ControlMask) {
		switch(ksym) {
		/* movement */
		case XK_d: /* fallthrough */
			if (next) {
				sel = curr = next;
				calcoffsets();
				goto draw;
			} else
				ksym = XK_G;
			break;
		case XK_u:
			if (prev) {
				sel = curr = prev;
				calcoffsets();
				goto draw;
			} else
				ksym = XK_g;
			break;
		case XK_p: /* fallthrough */
		case XK_P: break;
		case XK_c:
			cleanup();
			exit(1);
		case XK_Return: /* fallthrough */
		case XK_KP_Enter: break;
		default: return;
		}
	}

	switch(ksym) {
	/* movement */
	case XK_0:
		cursor = 0;
		break;
	case XK_dollar:
		if (text[cursor + 1] != '\0') {
			cursor = strlen(text) - 1;
			break;
		}
		break;
	case XK_b:
		movewordedge(-1);
		break;
	case XK_e:
		cursor = nextrune(+1);
		movewordedge(+1);
		if (text[cursor] == '\0')
			--cursor;
		else
			cursor = nextrune(-1);
		break;
	case XK_g:
		if (sel == matches) {
			break;
		}
		sel = curr = matches;
		calcoffsets();
		break;
	case XK_G:
		if (next) {
			/* jump to end of list and position items in reverse */
			curr = matchend;
			calcoffsets();
			curr = prev;
			calcoffsets();
			while (next && (curr = curr->right))
				calcoffsets();
		}
		sel = matchend;
		break;
	case XK_h:
		if (cursor)
			cursor = nextrune(-1);
		break;
	case XK_j:
		if (sel && sel->right && (sel = sel->right) == next) {
			curr = next;
			calcoffsets();
		}
		break;
	case XK_k:
		if (sel && sel->left && (sel = sel->left)->right == curr) {
			curr = prev;
			calcoffsets();
		}
		break;
	case XK_l:
		if (text[cursor] != '\0' && text[cursor + 1] != '\0')
			cursor = nextrune(+1);
		else if (text[cursor] == '\0' && cursor)
			--cursor;
		break;
	case XK_w:
		movewordedge(+1);
		if (text[cursor] != '\0' && text[cursor + 1] != '\0')
			cursor = nextrune(+1);
		else if (cursor)
			--cursor;
		break;
	/* insertion */
	case XK_a:
		cursor = nextrune(+1);
		/* fallthrough */
	case XK_i:
		using_vi_mode = 0;
		break;
	case XK_A:
		if (text[cursor] != '\0')
			cursor = strlen(text);
		using_vi_mode = 0;
		break;
	case XK_I:
		cursor = using_vi_mode = 0;
		break;
	case XK_p:
		if (text[cursor] != '\0')
			cursor = nextrune(+1);
		XConvertSelection(dpy, (ev->state & ControlMask) ? clip : XA_PRIMARY,
							utf8, utf8, win, CurrentTime);
		return;
	case XK_P:
		XConvertSelection(dpy, (ev->state & ControlMask) ? clip : XA_PRIMARY,
							utf8, utf8, win, CurrentTime);
		return;
	/* deletion */
	case XK_D:
		text[cursor] = '\0';
		if (cursor)
			cursor = nextrune(-1);
		match();
		break;
	case XK_x:
		cursor = nextrune(+1);
		insert(NULL, nextrune(-1) - cursor);
		if (text[cursor] == '\0' && text[0] != '\0')
			--cursor;
		match();
		break;
	/* misc. */
	case XK_Return:
	case XK_KP_Enter:
		if (use_text_input)
			puts((sel && (ev->state & ShiftMask)) ? sel->text : text);
		else
			puts((sel && !(ev->state & ShiftMask)) ? sel->text : text);
		if (!(ev->state & ControlMask)) {
			cleanup();
			exit(0);
		}
		if (sel)
			sel->out = 1;
		break;
	case XK_Tab:
		if (!sel)
			return;
		strncpy(text, sel->text, sizeof text - 1);
		text[sizeof text - 1] = '\0';
		cursor = strlen(text) - 1;
		match();
		break;
	default:
		for (size_t i = 0; i < quit_len; ++i)
			if (quit_keys[i].ksym == ksym &&
				(quit_keys[i].state & ev->state) == quit_keys[i].state) {
				cleanup();
				exit(1);
			}
	}

draw:
	drawmenu();
}

static void
keypress(XKeyEvent *ev)
{
	char buf[32];
	int len;
	KeySym ksym;
	Status status;

	len = XmbLookupString(xic, ev, buf, sizeof buf, &ksym, &status);
	switch (status) {
	default: /* XLookupNone, XBufferOverflow */
		return;
	case XLookupChars:
		goto insert;
	case XLookupKeySym:
	case XLookupBoth:
		break;
	}

	if (using_vi_mode) {
		vi_keypress(ksym, ev);
		return;
	} else if (vi_mode &&
			   (ksym == global_esc.ksym &&
				(ev->state & global_esc.state) == global_esc.state)) {
		using_vi_mode = 1;
		if (cursor)
			cursor = nextrune(-1);
		goto draw;
	}

	if (ev->state & ControlMask) {
		switch(ksym) {
		case XK_a: ksym = XK_Home;      break;
		case XK_b: ksym = XK_Left;      break;
		case XK_c: ksym = XK_Escape;    break;
		case XK_d: ksym = XK_Delete;    break;
		case XK_e: ksym = XK_End;       break;
		case XK_f: ksym = XK_Right;     break;
		case XK_g: ksym = XK_Escape;    break;
		case XK_h: ksym = XK_BackSpace; break;
		case XK_i: ksym = XK_Tab;       break;
		case XK_j: /* fallthrough */
		case XK_J: /* fallthrough */
		case XK_m: /* fallthrough */
		case XK_M: ksym = XK_Return; ev->state &= ~ControlMask; break;
		case XK_n: ksym = XK_Down;      break;
		case XK_p: ksym = XK_Up;        break;

		case XK_k: /* delete right */
			text[cursor] = '\0';
			match();
			break;
		case XK_u: /* delete left */
			insert(NULL, 0 - cursor);
			break;
		case XK_w: /* delete word */
			while (cursor > 0 && strchr(worddelimiters, text[nextrune(-1)]))
				insert(NULL, nextrune(-1) - cursor);
			while (cursor > 0 && !strchr(worddelimiters, text[nextrune(-1)]))
				insert(NULL, nextrune(-1) - cursor);
			break;
		case XK_y: /* paste selection */
		case XK_Y:
			XConvertSelection(dpy, (ev->state & ShiftMask) ? clip : XA_PRIMARY,
			                  utf8, utf8, win, CurrentTime);
			return;
		case XK_Left:
		case XK_KP_Left:
			movewordedge(-1);
			goto draw;
		case XK_Right:
		case XK_KP_Right:
			movewordedge(+1);
			goto draw;
		case XK_Return:
		case XK_KP_Enter:
			break;
		case XK_bracketleft:
			cleanup();
			exit(1);
		default:
			return;
		}
	} else if (ev->state & Mod1Mask) {
		switch(ksym) {
		case XK_b:
			movewordedge(-1);
			goto draw;
		case XK_f:
			movewordedge(+1);
			goto draw;
		case XK_g: ksym = XK_Home;  break;
		case XK_G: ksym = XK_End;   break;
		case XK_h: ksym = XK_Up;    break;
		case XK_j: ksym = XK_Next;  break;
		case XK_k: ksym = XK_Prior; break;
		case XK_l: ksym = XK_Down;  break;
		default:
			return;
		}
	}

	switch(ksym) {
	default:
  insert:
		if (!iscntrl((unsigned char)*buf))
			insert(buf, len);
		break;
	case XK_Delete:
	case XK_KP_Delete:
		if (text[cursor] == '\0')
			return;
		cursor = nextrune(+1);
		/* fallthrough */
	case XK_BackSpace:
		if (cursor == 0)
			return;
		insert(NULL, nextrune(-1) - cursor);
		break;
	case XK_End:
	case XK_KP_End:
		if (text[cursor] != '\0') {
			cursor = strlen(text);
			break;
		}
		if (next) {
			/* jump to end of list and position items in reverse */
			curr = matchend;
			calcoffsets();
			curr = prev;
			calcoffsets();
			while (next && (curr = curr->right))
				calcoffsets();
		}
		sel = matchend;
		break;
	case XK_Escape:
		cleanup();
		exit(1);
	case XK_Home:
	case XK_KP_Home:
		if (sel == matches) {
			cursor = 0;
			break;
		}
		sel = curr = matches;
		calcoffsets();
		break;
	case XK_Left:
	case XK_KP_Left:
		if (cursor > 0 && (!sel || !sel->left || lines > 0)) {
			cursor = nextrune(-1);
			break;
		}
		if (lines > 0)
			return;
		/* fallthrough */
	case XK_Up:
	case XK_KP_Up:
		if (sel && sel->left && (sel = sel->left)->right == curr) {
			curr = prev;
			calcoffsets();
		}
		break;
	case XK_Next:
	case XK_KP_Next:
		if (!next)
			return;
		sel = curr = next;
		calcoffsets();
		break;
	case XK_Prior:
	case XK_KP_Prior:
		if (!prev)
			return;
		sel = curr = prev;
		calcoffsets();
		break;
	case XK_Return:
	case XK_KP_Enter:
		if (print_index)
			printf("%d\n", (sel && !(ev->state & ShiftMask)) ? sel->index : -1);
		else
			puts((sel && !(ev->state & ShiftMask)) ? sel->text : text);

		if (!(ev->state & ControlMask)) {
			cleanup();
			exit(0);
		}
		if (sel)
			sel->out = 1;
		break;
	case XK_Right:
	case XK_KP_Right:
		if (text[cursor] != '\0') {
			cursor = nextrune(+1);
			break;
		}
		if (lines > 0)
			return;
		/* fallthrough */
	case XK_Down:
	case XK_KP_Down:
		if (sel && sel->right && (sel = sel->right) == next) {
			curr = next;
			calcoffsets();
		}
		break;
	case XK_Tab:
		if (!sel)
			return;
		cursor = strnlen(sel->text, sizeof text - 1);
		memcpy(text, sel->text, cursor);
		text[cursor] = '\0';
		match();
		break;
	}
	if (incremental) {
		puts(text);
		fflush(stdout);
	}
draw:
	drawmenu();
}

static void
buttonpress(XEvent *e)
{
	struct item *item;
	XButtonPressedEvent *ev = &e->xbutton;
	int x = 0, y = 0, h = bh, w;

	if (ev->window != win)
		return;

	/* right-click: exit */
	if (ev->button == Button3)
		exit(1);

	if (prompt && *prompt)
		x += promptw;

	/* input field */
	w = (lines > 0 || !matches) ? mw - x : inputw;

	/* left-click on input: clear input,
	 * NOTE: if there is no left-arrow the space for < is reserved so
	 *       add that to the input width */
	if (ev->button == Button1 &&
	   ((lines <= 0 && ev->x >= 0 && ev->x <= x + w +
	   ((!prev || !curr->left) ? TEXTW("<") : 0)) ||
	   (lines > 0 && ev->y >= y && ev->y <= y + h))) {
		insert(NULL, -cursor);
		drawmenu();
		return;
	}
	/* middle-mouse click: paste selection */
	if (ev->button == Button2) {
		XConvertSelection(dpy, (ev->state & ShiftMask) ? clip : XA_PRIMARY,
		                  utf8, utf8, win, CurrentTime);
		drawmenu();
		return;
	}
	/* scroll up */
	if (ev->button == Button4 && prev) {
		sel = curr = prev;
		calcoffsets();
		drawmenu();
		return;
	}
	/* scroll down */
	if (ev->button == Button5 && next) {
		sel = curr = next;
		calcoffsets();
		drawmenu();
		return;
	}
	if (ev->button != Button1)
		return;
	if (ev->state & ~ControlMask)
		return;
	if (lines > 0) {
		/* vertical list: (ctrl)left-click on item */
		w = mw - x;
		for (item = curr; item != next; item = item->right) {
			y += h;
			if (ev->y >= y && ev->y <= (y + h)) {
				puts(item->text);
				if (!(ev->state & ControlMask))
					exit(0);
				sel = item;
				if (sel) {
					sel->out = 1;
					drawmenu();
				}
				return;
			}
		}
	} else if (matches) {
		/* left-click on left arrow */
		x += inputw;
		w = TEXTW("<");
		if (prev && curr->left) {
			if (ev->x >= x && ev->x <= x + w) {
				sel = curr = prev;
				calcoffsets();
				drawmenu();
				return;
			}
		}
		/* horizontal list: (ctrl)left-click on item */
		for (item = curr; item != next; item = item->right) {
			x += w;
			w = MIN(TEXTW(item->text), mw - x - TEXTW(">"));
			if (ev->x >= x && ev->x <= x + w) {
				puts(item->text);
				if (!(ev->state & ControlMask))
					exit(0);
				sel = item;
				if (sel) {
					sel->out = 1;
					drawmenu();
				}
				return;
			}
		}
		/* left-click on right arrow */
		w = TEXTW(">");
		x = mw - w;
		if (next && ev->x >= x && ev->x <= x + w) {
			sel = curr = next;
			calcoffsets();
			drawmenu();
			return;
		}
	}
}

static void
paste(void)
{
	char *p, *q;
	int di;
	unsigned long dl;
	Atom da;

	/* we have been given the current selection, now insert it into input */
	if (XGetWindowProperty(dpy, win, utf8, 0, (sizeof text / 4) + 1, False,
	                   utf8, &da, &di, &dl, &dl, (unsigned char **)&p)
	    == Success && p) {
		insert(p, (q = strchr(p, '\n')) ? q - p : (ssize_t)strlen(p));
		XFree(p);
	}
	if (using_vi_mode && text[cursor] == '\0')
		--cursor;
	drawmenu();
}

static void
readstdin(FILE* stream)
{
	char *line = NULL;
	size_t i, junk, size = 0;
	ssize_t len;

	if(passwd){
    inputw = lines = 0;
    return;
  }

	/* read each line from stdin and add it to the item list */
	for (i = 0; (len = getline(&line, &junk, stream)) != -1; i++, line = NULL) {
	// for (i = 0; fgets(buf, sizeof buf,stream);   i++) {
		if (i + 1 >= size / sizeof *items)
			if (!(items = realloc(items, (size += BUFSIZ))))
				die("cannot realloc %zu bytes:", size);
		if (line[len - 1] == '\n')
			line[len - 1] = '\0';
		items[i].text = line;
		items[i].out = 0;
		items[i].index = i;
	}
	if (items)
		items[i].text = NULL;
	lines = MIN(max_lines, i);
}

void
resource_load(XrmDatabase db, char *name, enum resource_type rtype, void *dst)
{
	char *sdst = NULL;
	int *idst = NULL;
	float *fdst = NULL;
	sdst = dst;
	idst = dst;
	fdst = dst;
	char fullname[256];
	char *type;
	XrmValue ret;
	snprintf(fullname, sizeof(fullname), "%s.%s", "dmenu", name);
	fullname[sizeof(fullname) - 1] = '\0';
	XrmGetResource(db, fullname, "*", &type, &ret);
	if (!(ret.addr == NULL || strncmp("String", type, 64)))
	{
		switch (rtype) {
		case STRING:
			strcpy(sdst, ret.addr);
			break;
		case INTEGER:
			*idst = strtoul(ret.addr, NULL, 10);
			break;
		case FLOAT:
			*fdst = strtof(ret.addr, NULL);
			break;
		}
	}
}

void
load_xresources(void)
{
	Display *display;
	char *resm;
	XrmDatabase db;
	ResourcePref *p;
	display = XOpenDisplay(NULL);
	resm = XResourceManagerString(display);
	if (!resm)
		return;
	db = XrmGetStringDatabase(resm);
	for (p = resources; p < resources + LENGTH(resources); p++)
		resource_load(db, p->name, p->type, p->dst);
	XCloseDisplay(display);
}

static void
run(void)
{
	XEvent ev;

	while (!XNextEvent(dpy, &ev)) {
		if (XFilterEvent(&ev, win))
			continue;
		switch(ev.type) {
		case DestroyNotify:
			if (ev.xdestroywindow.window != win)
				break;
			cleanup();
			exit(1);
		case ButtonPress:
			buttonpress(&ev);
			break;
		case Expose:
			if (ev.xexpose.count == 0)
				drw_map(drw, win, 0, 0, mw, mh);
			break;
		case FocusIn:
			/* regrab focus from parent window */
			if (ev.xfocus.window != win)
				grabfocus();
			break;
		case KeyPress:
			keypress(&ev.xkey);
			break;
		case SelectionNotify:
			if (ev.xselection.property == utf8)
				paste();
			break;
		case VisibilityNotify:
			if (ev.xvisibility.state != VisibilityUnobscured)
				XRaiseWindow(dpy, win);
			break;
		}
	}
}

static void
setup(void)
{
	int x, y, i, j;
	unsigned int du;
	XSetWindowAttributes swa;
	XIM xim;
	Window w, dw, *dws;
	XWindowAttributes wa;
	XClassHint ch = {"dmenu", "dmenu"};
#ifdef XINERAMA
	XineramaScreenInfo *info;
	Window pw;
	int a, di, n, area = 0;
#endif
	/* init appearance */
	for (j = 0; j < SchemeLast; j++)
		scheme[j] = drw_scm_create(drw, colors[j], alphas[i], 2);

	clip = XInternAtom(dpy, "CLIPBOARD",   False);
	utf8 = XInternAtom(dpy, "UTF8_STRING", False);

	/* calculate menu geometry */
	bh = drw->fonts->h + 2;
	bh = MAX(bh,lineheight);	/* make a menu line AT LEAST 'lineheight' tall */
	lines = MAX(lines, 0);
	mh = (lines + 1) * bh;
#ifdef XINERAMA
	i = 0;
	if (parentwin == root && (info = XineramaQueryScreens(dpy, &n))) {
		XGetInputFocus(dpy, &w, &di);
		if (mon >= 0 && mon < n)
			i = mon;
		else if (w != root && w != PointerRoot && w != None) {
			/* find top-level window containing current input focus */
			do {
				if (XQueryTree(dpy, (pw = w), &dw, &w, &dws, &du) && dws)
					XFree(dws);
			} while (w != root && w != pw);
			/* find xinerama screen with which the window intersects most */
			if (XGetWindowAttributes(dpy, pw, &wa))
				for (j = 0; j < n; j++)
					if ((a = INTERSECT(wa.x, wa.y, wa.width, wa.height, info[j])) > area) {
						area = a;
						i = j;
					}
		}
		/* no focused window is on screen, so use pointer location instead */
		if (mon < 0 && !area && XQueryPointer(dpy, root, &dw, &dw, &x, &y, &di, &di, &du))
			for (i = 0; i < n; i++)
				if (INTERSECT(x, y, 1, 1, info[i]) != 0)
					break;

		x = info[i].x_org + dmx;
		y = info[i].y_org + (topbar ? dmy : info[i].height - mh - dmy);
		mw = (dmw>0 ? dmw : info[i].width);

		XFree(info);
	} else
#endif
	{
		if (!XGetWindowAttributes(dpy, parentwin, &wa))
			die("could not get embedding window attributes: 0x%lx",
			    parentwin);
		x = dmx;
		y = topbar ? dmy : wa.height - mh - dmy;
		mw = (dmw>0 ? dmw : wa.width);
	}
	promptw = (prompt && *prompt) ? TEXTW(prompt) - lrpad / 4 : 0;
	inputw = mw / 3; /* input width: ~33% of monitor width */
	match();
	for (i = 0; i < preselected; i++) {
		if (sel && sel->right && (sel = sel->right) == next) {
			curr = next;
			calcoffsets();
		}
	}

	/* create menu window */
	swa.override_redirect = True;
	swa.background_pixel = 0;
	swa.border_pixel = 0;
	swa.colormap = cmap;
	swa.event_mask = ExposureMask | KeyPressMask | VisibilityChangeMask | ButtonPressMask;
	win = XCreateWindow(dpy, parentwin, x, y, mw, mh, border_width,
	                    depth, CopyFromParent, visual,
	                    CWOverrideRedirect | CWBackPixel | CWBorderPixel | CWColormap | CWEventMask, &swa);
	if (border_width)
		XSetWindowBorder(dpy, win, scheme[SchemeSel][ColBg].pixel);
	XSetClassHint(dpy, win, &ch);


	/* input methods */
	if ((xim = XOpenIM(dpy, NULL, NULL, NULL)) == NULL)
		die("XOpenIM failed: could not open input device");

	xic = XCreateIC(xim, XNInputStyle, XIMPreeditNothing | XIMStatusNothing,
	                XNClientWindow, win, XNFocusWindow, win, NULL);

	XMapRaised(dpy, win);
	if (embed) {
		XSelectInput(dpy, parentwin, FocusChangeMask | SubstructureNotifyMask);
		if (XQueryTree(dpy, parentwin, &dw, &w, &dws, &du) && dws) {
			for (i = 0; i < du && dws[i] != win; ++i)
				XSelectInput(dpy, dws[i], FocusChangeMask);
			XFree(dws);
		}
		grabfocus();
	}
	drw_resize(drw, mw, mh);
	drawmenu();
}

static void
usage(void)
{
	die("Help:\nsyntax : dmenu [opciones] \n"
      "usage: dmenu [-bfiv] \n"
      "-b     dmenu appears at the bottom of the screen.\n"
      "-f     dmenu grabs the keyboard before reading stdin if not reading from a tty. This is faster, but will\n"
	    "-F     option; if 0, dmenu doesn't use fuzzy matching\n"
      "-i     dmenu matches menu items case insensitively.\n"
	    "-r     dmenu outputs text each time a key is pressed.\n"
      "-ix    dmenu prints the index of matched text instead of the text itself.\n"
      "-t     Return key prints input text instead of selection.\n"
      "-v     prints version information to stdout, then exits.\n"
      "-vi    mode to use when -vi is passed."
      "[-l lines]   dmenu lists items vertically, with the given number of lines. \n"
      "[-h height]  dmenu uses a menu line of at least 'height' pixels tall, but no less than 8.\n"
      "[-m monitor] dmenu is displayed on the monitor number supplied. Monitor numbers are starting from 0.\n"
      "[-x xoffset] dmenu is placed at this offset measured from the left side of the monitor.  Can be negative.\n\tIf option -m is present, the measurement will use the given monitor. \n"
      "[-y yoffset] dmenu  is  placed at this offset measured from the top of the monitor.  If the -b option is used the offset is measured from the bottom.  Can be negative.  If option -m is present, the  measure‐ment will use the given monitor.\n"
      "[-z width]   sets the width of the dmenu window.\n"
      "[-p prompt]  defines the prompt to be displayed to the left of the input field.\n"
      "[-fn font]   defines the font or font set used.\n"
      "[-nb color]  defines the normal background color.  #RGB, #RRGGBB, and X color names are supported.\n"
      "[-nf color]  defines the normal foreground color.\n"
      "[-sb color]  defines the selected background color.\n"
      "[-sf color]  defines the selected foreground color.\n"
	    "[-w windowid] embed into windowid.\n"
      "[-n number]  preseslected item starting from 0.\n"
      "[-dy command] runs command whenever input changes to update menu items.\n"
      "[-wb BorderWidth] defines the width of the border around the dmenu window. \n"
      "[-ix print_index] returns index of matched text instead of the text itself. \n", stderr);
}

int
main(int argc, char *argv[])
{
	XWindowAttributes wa;
	int i, fast = 0;

	XrmInitialize();
	load_xresources();
	
	for (i = 1; i < argc; i++)
		/* these options take no arguments */
		if (!strcmp(argv[i], "-v")) {      /* prints version information */
			puts("dmenu-"VERSION);
			exit(0);
		} else if (!strcmp(argv[i], "-b")) /* appears at the bottom of the screen */
			topbar = 0;
		else if (!strcmp(argv[i], "-f"))   /* grabs keyboard before reading stdin */
			fast = 1;
		else if (!strcmp(argv[i], "-F"))   /* grabs keyboard before reading stdin */
			fuzzy = 0;
		else if (!strcmp(argv[i], "-r"))   /* incremental */
			incremental = 1;
		else if (!strcmp(argv[i], "-i")) { /* case-insensitive item matching */
			fstrncmp = strncasecmp;
			fstrstr = cistrstr;
		} else if (!strcmp(argv[i], "-t")) /* favors text input over selection */
			use_text_input = 1;
		 else if (!strcmp(argv[i], "-vi")) {
			vi_mode = 1;
			using_vi_mode = start_mode;
			global_esc.ksym = XK_Escape;
			global_esc.state = 0;
		} else if (!strcmp(argv[i], "-ix"))  /* adds ability to return index in list */
			print_index = 1;
		else if (!strcmp(argv[i], "-P"))   /* is the input a password */
			passwd = 1;
    else if (i + 1 == argc)
			usage();
		/* these options take one argument */
		else if (!strcmp(argv[i], "-l"))   /* number of lines in vertical list */
			lines = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-h")) { /* minimum height of one menu line */
			lineheight = atoi(argv[++i]);
			lineheight = MAX(lineheight, min_lineheight);
		} else if (!strcmp(argv[i], "-x"))   /* window x offset */
			dmx = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-y"))   /* window y offset (from bottom up if -b) */
			dmy = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-z"))   /* make dmenu this wide */
			dmw = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-m"))
			mon = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-p"))   /* adds prompt to left of input field */
			prompt = argv[++i];
		else if (!strcmp(argv[i], "-fn"))  /* font or font set */
			fonts[0] = argv[++i];
		else if (!strcmp(argv[i], "-nb"))  /* normal background color */
			colors[SchemeNorm][ColBg] = argv[++i];
		else if (!strcmp(argv[i], "-nf"))  /* normal foreground color */
			colors[SchemeNorm][ColFg] = argv[++i];
		else if (!strcmp(argv[i], "-sb"))  /* selected background color */
			colors[SchemeSel][ColBg] = argv[++i];
		else if (!strcmp(argv[i], "-sf"))  /* selected foreground color */
			colors[SchemeSel][ColFg] = argv[++i];
		else if (!strcmp(argv[i], "-w"))   /* embedding window id */
			embed = argv[++i];
		else if (!strcmp(argv[i], "-n"))   /* preselected item */
			preselected = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-dy"))  /* dynamic command to run */
			dynamic = argv[++i] && *argv[i] ? argv[i] : NULL;
		else if (!strcmp(argv[i], "-bw"))
			border_width = atoi(argv[++i]); /* border width */
		else
			usage();

	if (!setlocale(LC_CTYPE, "") || !XSupportsLocale())
		fputs("warning: no locale support\n", stderr);
	if (!(dpy = XOpenDisplay(NULL)))
		die("cannot open display");
	screen = DefaultScreen(dpy);
	root = RootWindow(dpy, screen);
	if (!embed || !(parentwin = strtol(embed, NULL, 0)))
		parentwin = root;
	if (!XGetWindowAttributes(dpy, parentwin, &wa))
		die("could not get embedding window attributes: 0x%lx",
		    parentwin);
	xinitvisual();
	drw = drw_create(dpy, screen, root, wa.width, wa.height, visual, depth, cmap);
	if (!drw_fontset_create(drw, fonts, LENGTH(fonts)))
		die("no fonts could be loaded.");
	lrpad = drw->fonts->h;

    if (lineheight == -1)
        lineheight = drw->fonts->h * 2.5;

#ifdef __OpenBSD__
	if (pledge("stdio rpath", NULL) == -1)
		die("pledge");
#endif

	max_lines = lines;
	if (fast && !isatty(0)) {
		grabkeyboard();
		if (!dynamic)
			readstdin(stdin);
	} else {
		if (!dynamic)
			readstdin(stdin);
		grabkeyboard();
	}
	setup();
	run();

	return 1; /* unreachable */
}

void
xinitvisual()
{
	XVisualInfo *infos;
	XRenderPictFormat *fmt;
	int nitems;
	int i;

	XVisualInfo tpl = {
		.screen = screen,
		.depth = 32,
		.class = TrueColor
	};
	long masks = VisualScreenMask | VisualDepthMask | VisualClassMask;

	infos = XGetVisualInfo(dpy, masks, &tpl, &nitems);
	visual = NULL;
	for(i = 0; i < nitems; i ++) {
		fmt = XRenderFindVisualFormat(dpy, infos[i].visual);
		if (fmt->type == PictTypeDirect && fmt->direct.alphaMask) {
			 visual = infos[i].visual;
			 depth = infos[i].depth;
			 cmap = XCreateColormap(dpy, root, visual, AllocNone);
			 useargb = 1;
			 break;
		}
	}

	XFree(infos);

	if (! visual) {
		visual = DefaultVisual(dpy, screen);
		depth = DefaultDepth(dpy, screen);
		cmap = DefaultColormap(dpy, screen);
	}
}
