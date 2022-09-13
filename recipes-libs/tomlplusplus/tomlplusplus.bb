SUMMARY = "C++ header-only TOML parsing library"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=90960f22c10049c117d56ed2ee5ee167"
PV = "1.0+git${SRCPV}"

# fetch from git
SRCBRANCH ?= "master"
SRCREV_base = "v3.2.0"

SRC_URI = "git://github.com/marzer/tomlplusplus.git;protocol=https;branch=${SRCBRANCH};name=base"

S = "${WORKDIR}/git"

inherit pkgconfig cmake
