set name z7_20_hdmi

platform -name $name \
	-desc "A hdmi SDSoc platform for the Zybo Z7 20" \
	-hw [format ../%s.dsa $name] \
	-prebuilt \
	-samples ./resources/samples \
	-out ./output 

system -name linux -display-name "Linux" -boot ./resources/linux/boot \
		   -readme ./resources/linux/boot/generic.readme

domain -name linux -proc ps7_cortexa9 -os linux -image ./resources/linux/image 

domain -prebuilt-data ./resources/prebuilt

boot -bif ./resources/linux/linux.bif

library -inc-path ./resources/linux/inc/xfopencv

platform -generate
