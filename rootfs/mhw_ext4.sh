#!/bin/bash
rm rootfs.ext4
dd if=/dev/zero of=rootfs.ext4 bs=1M count=300
sudo mkfs.ext4 rootfs.ext4
mkdir temp
sudo mount -o loop rootfs.ext4 ./temp/
sudo cp fs_debian_jessie/* temp/ -rf
sudo umount ./temp
rmdir temp
