# PlatformIO pre-script for the native test env. It runs before library
# builders clone the environment, so the flag reaches dependency C sources.
# ricmoo/QRCode typedefs bool for C, which GCC 15 (C23 default) rejects.
Import("env")

env.Append(CFLAGS=["-std=gnu17"])
