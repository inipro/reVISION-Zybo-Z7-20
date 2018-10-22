# reVISION-Zybo-Z7-20
Zybo Z7-20 reVISION Platform

## vivado
$ vivaeo -nolog -nojournal -mode batch -source z7_20_hdmi.tcl

$ cd z7_20_htmi

$ vivado z7_20_hdmi.xpr

Gernerate Bitstream

File -> Export -> Export Hardware(Include bitstream)

## petalinux
See petalinux/BUILD.txt, petalinux/TEST.txt

## sdsoc
$ cd z7_20_hdmi

$ vivado z7_20_hdmi.xpr

Open Block Design

Tcl Console : source ../create_dsa.tcl

$ cd sdsoc

$ sh copy.sh

$ xsct platform.tcl

$ sdx

