set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)

set(CMAKE_SYSROOT /usr/aarch64-linux-gnu)
set(CMAKE_FIND_ROOT_PATH /tmp/arm64-sysroot /usr/aarch64-linux-gnu)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

set(CMAKE_EXE_LINKER_FLAGS_INIT "-L/tmp/arm64-sysroot/lib")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-L/tmp/arm64-sysroot/lib")

set(ENV{PKG_CONFIG_PATH} "/tmp/arm64-sysroot/lib/pkgconfig")
set(ENV{PKG_CONFIG_SYSROOT_DIR} "/tmp/arm64-sysroot")
set(ENV{PKG_CONFIG_LIBDIR} "/tmp/arm64-sysroot/lib/pkgconfig")
