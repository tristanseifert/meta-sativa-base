SUMMARY = "Portable tun/tap library"
LICENSE = "ISC"
LIC_FILES_CHKSUM = "file://${COREBASE}/meta/files/common-licenses/ISC;md5=f3b90e78ea0cffb20bf5cca7947a896d"
PV = "1.0+git${SRCPV}"

# fetch the library from git
SRCBRANCH ?= "master"
SRCREV_base = "${AUTOREV}"

SRC_URI = "git://github.com/LaKabane/libtuntap.git;protocol=https;branch=${SRCBRANCH};name=base"

# build it as cmake
S = "${WORKDIR}/git"

inherit pkgconfig cmake
