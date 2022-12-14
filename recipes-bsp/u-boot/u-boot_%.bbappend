FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

# custom version modifier
UBOOT_LOCALVERSION = "-sativa"

# use a newer u-boot (v2022.07)
SRCREV = "e092e3250270a1016c877da7bdd9384f14b1321e"
LIC_FILES_CHKSUM = "file://Licenses/README;md5=2ca5f2c35c8cc335f0a19756634782f1"

# extlinux config
# TODO: can we make this not hard-code the version? symlink support w/ erofs is broken
UBOOT_EXTLINUX_KERNEL_IMAGE = "/boot/zImage-5.15.72"
UBOOT_EXTLINUX_KERNEL_IMAGE_default = "/boot/zImage-5.15.72"

# include the basic STM32MP15x device trees
SRC_URI:append:stm32mp1 = " \
    file://git/arch/arm/dts/stm32mp15-mx.dtsi \
    file://git/arch/arm/dts/stm32mp15xa.dtsi \
    file://git/arch/arm/dts/stm32mp15-m4-srm.dtsi \
    file://git/include/dt-bindings/pinctrl/stm32-pinfunc-2.h \
"

# remove some CVE patches from upstream (they conflict with newer u-boot)
SRC_URI:remove = "\
    file://0001-i2c-fix-stack-buffer-overflow-vulnerability-in-i2c-m.patch\
    file://0001-fs-squashfs-sqfs_read-Prevent-arbitrary-code-executi.patch\
    file://0001-net-Check-for-the-minimum-IP-fragmented-datagram-siz.patch\
    file://0001-fs-squashfs-Use-kcalloc-when-relevant.patch\
"
