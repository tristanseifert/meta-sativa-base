require linux-mainline-common.inc

# Linux 5.15.x, a longer-term release
LINUX_VERSION ?= "5.15.x"
KERNEL_VERSION_SANITY_SKIP="1"

BRANCH = "linux-5.15.y"

SRCREV = "${AUTOREV}"

# fetch from kernel git repo, and apply some basic config
SRC_URI = " \
    git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git;branch=${BRANCH} \
    file://filesystems.cfg \
    file://less-drivers.cfg \
    file://base.cfg \
"

# resource manager driver (for STM32MP15x)
SRC_URI += " \
    file://git/drivers/remoteproc/rproc_srm_dev.c \
    file://git/drivers/remoteproc/rproc_srm_core.h \
    file://git/drivers/remoteproc/rproc_srm_core.c \
    file://0003-add-resource-manager.patch \
    file://0004-add-resource-manager-probe.patch \
"
