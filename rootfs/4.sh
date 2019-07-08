#!/bin/bash
sudo rm fs_debian_jessie/dev/*
sudo ./usr/local/bin/make_ext4fs -s -l 996147200 -a root -L linux system.img fs_debian_jessie
