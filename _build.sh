#!/bin/sh

### checkout correct prima sources if needed
PRIMA_PATH="drivers/staging/prima"
PRIMA_GIT="https://github.com/adrian-bl-yuga/caf_prima.git"

if [ ! -d ${PRIMA_PATH}/.git ] ; then
	echo "found in-kernel prima sources, replacing them"
	echo "with a copy of $PRIMA_GIT in 2 sec..."
	sleep 2

	echo "-> removing old source tree..."
	rm -rf $PRIMA_PATH

	echo "-> cloning $PRIMA_GIT"
	git clone $PRIMA_GIT $PRIMA_PATH
fi
### end prima


export TCHAIN=../../../prebuilts/gcc/linux-x86/arm/arm-eabi-4.8/bin/arm-eabi-

make ARCH=arm clean
make ARCH=arm CROSS_COMPILE=$TCHAIN ns_yuga_defconfig
make ARCH=arm CROSS_COMPILE=$TCHAIN -j 8 > build.log 2>&1

# add new kernel
cp arch/arm/boot/zImage ../../../device/sony/c6603/kernel 

# drop kernel modules
cp ./drivers/staging/prima/wlan.ko  ../../../vendor/sony/yuga_blobs/system/lib/modules/wlan.ko
# cp ./drivers/media/radio/radio-iris-transport.ko ../../../vendor/sony/yuga_blobs/system/lib/modules/radio-iris-transport.ko

# refresh kernel headers
( cd ../../../device/sony/lagan/ && sh update_kernel_headers.sh )
