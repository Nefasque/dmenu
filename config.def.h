/* See LICENSE file for copyright and license details. */
/* Default settings; can be overriden by command line. */

static int topbar = 1;                     /* -b  option; if 0, dmenu appears at bottom     */
static int incremental = 0;                /* -r  option; if 1, outputs text each time a key is pressed */
static int fuzzy = 1;                      /* -F  option; if 0, dmenu doesn't use fuzzy matching     */
static int colorprompt = 1;                /* -p  option; if 1, prompt uses SchemeSel, otherwise SchemeNorm */
static const unsigned int alpha = 0xc0;    /* Amount of opacity. 0xff is opaque             */
/* -fn option overrides fonts[0]; default X11 font or font set */
static char font[] = "monospace:pixelsize=18:antialias=true:autohint=true";
static const char *fonts[] = {
  font,
  "monospace:size=10",
};

/* -p  option; prompt to the left of input field */
static char *prompt      = NULL;
/* -dy option; dynamic command to run on input change */
static const char *dynamic     = NULL;
static const char *symbol_1 = "";
static const char *symbol_2 = "";

/* ***************
 * Color schemes 
 * *****************
 * this color table is used for the default theme.
 * the Xresources file is used to override it.
 */
static char color0[] = "#222222";
static char color1[] = "#aa3333";
static char color2[] = "#33aa33";
static char color3[] = "#faea33";
static char color4[] = "#3333aa";
static char color5[] = "#aa33aa";
static char color6[] = "#aaaaff";
static char color7[] = "#ffffff";
static char color8[] = "#bbbbbb";

static char *colors[SchemeLast][10] = {
    [SchemeNorm] =      { color7, color0, color0},
    [SchemeSel] =       { color0, color1, color1},
    [SchemeOut] =       { "#000000", "#00ffff", "#00ffff" },
    [SchemeHighlight] = { color0, color1, color8},
    [SchemeHover] =     { color0, color1, color8 },
    [SchemeRed] =       { color0, color7, color0 },
    [SchemeGreen] =     { color0, color2, color0 },
    [SchemeYellow] =    { color0, color3, color0 },
    [SchemeBlue] =      { color0, color4, color0 },
    [SchemePurple] =    { color0, color5, color0 },
};

static unsigned int alphas[SchemeLast][2] = {
	[SchemeNorm] = { OPAQUE, alpha },
	[SchemeSel] = { OPAQUE, alpha },
	[SchemeOut] = { OPAQUE, alpha },
};
/* -l option; if nonzero, dmenu uses vertical list with given number of lines */
static unsigned int lines      = 0;
/* -h option; minimum height of a menu line */
static unsigned int lineheight = 0;
static unsigned int min_lineheight = 8;
static int sely = 0;
static int commented = 0;
static int animated = 0;
/* Characters not considered part of a word while deleting words
 * for example: " /?\"&[]" */
static const char worddelimiters[] = " ";


/* -n option; preselected item starting from 0 */
static unsigned int preselected = 0;

/* Size of the window border */
static unsigned int border_width = 0;

/* vi option; if nonzero, vi mode is always enabled and can be
 * accessed with the global_esc keysym + mod mask */
static unsigned int vi_mode = 1;
static unsigned int start_mode = 0;			/* mode to use when -vi is passed. 0 = insert mode, 1 = normal mode */
static Key global_esc = { XK_n, Mod1Mask };	/* escape key when vi mode is not enabled explicitly */
static Key quit_keys[] = {
	/* keysym	modifier */
	{ XK_q,		0 }
};

/* Xresources preferences to load at startup */
ResourcePref resources[] = {
	{ "font",        STRING, &font },
  { "color0",      STRING, &color0 },
  { "color1",      STRING, &color1 },
  { "color2",      STRING, &color2 },
  { "color3",      STRING, &color3 },
  { "color4",      STRING, &color4 },
  { "color5",      STRING, &color5 },
  { "color6",      STRING, &color6 },
  { "color7",      STRING, &color7 },
  { "color8",      STRING, &color8 },
	{ "prompt",      STRING, &prompt },
};

