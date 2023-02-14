#!/bin/bash

JOBS=`grep -c ^processor /proc/cpuinfo`
export CROSS_COMPILE=aarch64-linux-gnu-
make O=build-allwinner-a133 allwinner_a133_defconfig
make O=build-allwinner-a133 -j${JOBS}