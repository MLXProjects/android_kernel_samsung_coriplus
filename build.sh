#!/bin/bash
export ARCH=arm
export CROSS_COMPILE=/home/maicoljaureguiv1/projects/oldtc/bin/arm-linux-androideabi-
make bcm21654_rhea_ss_coriplus_rev00_defconfig
make -j3
