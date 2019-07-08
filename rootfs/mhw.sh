#!/bin/bash
sudo apt-get install binfmt-support qemu qemu-user-static debootstrap
sudo debootstrap --arch=armel --foreign jessie fs_debian_jessie http://mirrors.tuna.tsinghua.edu.cn/debian
sudo cp /usr/bin/qemu-arm-static fs_debian_jessie/usr/bin
sudo DEBIAN_FRONTEND=noninteractive DEBCONF_NONINTERACTIVE_SEEN=true LC_ALL=C LANGUAGE=C LANG=C chroot fs_debian_jessie debootstrap/debootstrap --second-stage
