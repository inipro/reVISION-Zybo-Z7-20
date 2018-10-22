#!/bin/sh

[ -f *.so ] && /bin/cp *.so $HOME/lib
[ -f *.elf ] && /bin/cp *.elf $HOME/bin
/bin/cp BOOT.BIN /media/card
/bin/cp -r _sds /media/card
