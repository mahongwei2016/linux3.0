#!/bin/bash

cd iTop4412_uboot/

./clean.sh 

cd ../

cd iTop4412_Kernel_3.0

./clean.sh
cd ../
#cp linux-4.14.2_iTop-4412_scp/arch/arm/boot/dts/exynos4412-itop-elite.dtb out/
#cp linux-4.14.2_iTop-4412_scp/arch/arm/boot/uImage out/
