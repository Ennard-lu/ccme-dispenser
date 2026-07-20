#!/bin/bash
set -e

cd /mnt/c/Users/ZijunLu/Libs/ccme-dispenser/build

cmake .. \
    -DCMAKE_TOOLCHAIN_FILE=../cmake/aarch64-toolchain.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DSDBUS_CPP_BUILD_LIBSYSTEMD=OFF \
    2>&1
