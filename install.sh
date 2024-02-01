read -p "you want to build and install? [Y/n] :" ans
ans=${ans,,}

if [ "$ans" == "y" ] || [ "$ans" = "" ] ; then
  sudo make uninstall
  sudo make clean install
fi

read -p "you want remove file of build? [Y/n] :" ans
ans=${ans,,}

if [ "$ans" == "y" ] || [ "$ans" = "" ] ; then
  rm -vrf dmenu
  rm -vrf config.h
  rm -vf *.o 
  rm -vf *.orig
  rm -vf *.rej
  echo "done"
  exit
fi

# install script 
read -p "you want install script? [Y/n] :" ans
ans=${ans,,}
if [ans == "y" ] || [ans == ""] ; then
  sudo cp -vrf script/* /usr/bin
  sudo cp -vrf ./script/desktop/* /usr/share/applications/
fi
