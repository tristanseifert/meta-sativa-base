# Sativa distribution layer
A Yocto layer to provide a basic Linux distribution for embedded platforms. It contains some common platform support stuff here.

## Dependencies
Other layers required to build the system are:

- meta-openembedded: specifically, meta-oe and meta-filesystems
- meta-openamp
- meta-rauc
- meta-clang: Optional, to compile everything with clang. This is the only compiler that's tested with these.

## Build configuration
First, select the distro `sativa` in your build configuration; then configure your `build/conf/local.conf` to use one of the following machines:

- stm32mp157a-dk1: STM32MP1 development board

You may specify custom machines based on the hardware supported here.

Additionally, you may wish to enable additional image features for debugging:

```
EXTRA_IMAGE_FEATURES = "debug-tweaks tools-debug"
```

Note that the flag to enable a read-only rootfs (`read-only-rootfs`) is not specified, as this causes some configuration (namely Dropbear) to get overridden such that the host key is _not_ stored on the overlay in /etc, and instead a ramdisk.

Typically, you will build the `core-image-base` image flavor.
