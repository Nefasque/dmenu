cd /home/nefasque/workspace/dmenu-5.2/

read -p "you want to build ? [Y/n] :" ans
ans=${ans,,}

if [ "$ans" == "y" ] || [ "$ans" = "" ] ; then
  sudo make uninstall
  sudo make clean install
fi



read -p "you want remove file of build ? [Y/n] :" ans
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

# remove all the stuff we don't need
