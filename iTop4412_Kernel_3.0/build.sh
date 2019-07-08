#!/bin/sh
#cp config_for_android_scp_elite .config
make zImage
cp arch/arm/boot/zImage ../out

