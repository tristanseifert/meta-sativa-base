SUMMARY = "Portable tun/tap library"
LICENSE = "ISC"
LIC_FILES_CHKSUM = "file://${COREBASE}/meta/files/common-licenses/ISC;md5=f3b90e78ea0cffb20bf5cca7947a896d"

# fetch the library from git
SRCBRANCH ?= "master"
SRCREV_base = "ef7ed7b4441624f825c1798ae79e0ddfc8edd4cb"

SRC_URI = "git://github.com/LaKabane/libtuntap.git;protocol=https;branch=${SRCBRANCH};name=base"

# build it as cmake
S = "${WORKDIR}/git"

inherit pkgconfig cmake
