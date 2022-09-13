SUMMARY = "A smart and easy to use C++ SQLite3 wrapper"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE.txt;md5=a6abb8f9ddcf3b84c6995c2de97a2f1c"
PV = "3.1.1+git${SRCPV}"

DEPENDS += "sqlite3"

SRCBRANCH ?= "master"
SRCREV_base = "3.1.1"

SRC_URI = "git://github.com/SRombauts/SQLiteCpp.git;protocol=https;branch=${SRCBRANCH};name=base"

S = "${WORKDIR}/git"

inherit pkgconfig cmake
