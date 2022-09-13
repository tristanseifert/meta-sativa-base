SUMMARY = "Crypto++ cryptographic library"
LICENSE = "BSD-3-Clause"
LIC_FILES_CHKSUM = "file://LICENSE;md5=dfbf91cfaad9e4491bd9e7ff57b59122"

# fetch the library from git
SRCBRANCH ?= "master"
# latest tip of master
SRCREV_base = "c0f97430d904ead8a903f51603afaa6b1d97d003"

SRC_URI = "git://github.com/abdes/cryptopp-cmake.git;protocol=https;branch=${SRCBRANCH};name=base"

# build it as cmake
S = "${WORKDIR}/git"

inherit pkgconfig cmake

