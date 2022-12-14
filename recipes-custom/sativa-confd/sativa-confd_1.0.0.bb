SUMMARY = "Config daemon"
LICENSE = "ISC"
LIC_FILES_CHKSUM = "file://${COREBASE}/meta/files/common-licenses/ISC;md5=f3b90e78ea0cffb20bf5cca7947a896d"
PR = "r0"
DEPENDS = "libcbor systemd git libevent fmt plog tomlplusplus sqlitecpp"
RDEPENDS:${PN} = "libsystemd"

# define the CMake source directories
SRC_URI = "\
    file://src/ \
    file://include/ \
    file://CMakeLists.txt \
"

# simply use cmake
S = "${WORKDIR}"

inherit pkgconfig cmake

# install systemd unit
REQUIRED_DISTRO_FEATURES= "systemd"

inherit systemd features_check

SYSTEMD_AUTO_ENABLE = "enable"
SYSTEMD_SERVICE:${PN} = "confd.service"

SRC_URI:append = " file://data/confd.service "
FILES:${PN} += "${systemd_unitdir}/system/confd.service"

do_install:append() {
    install -d ${D}/${systemd_unitdir}/system
    install -m 0644 ${WORKDIR}/data/confd.service ${D}/${systemd_unitdir}/system
}

# install configuration files
SRC_URI:append = " file://data/confd.toml "
FILES:${PN} += "/usr/etc/confd.toml /usr/etc/confd.d/ "

do_install:append() {
    install -d ${D}/usr/etc
    install -o confd -m 0644 ${WORKDIR}/data/confd.toml ${D}/usr/etc

    # template directory for bonus config files
    install -d ${D}/usr/etc/confd.d/
}

# create users
inherit useradd
USERADD_PACKAGES = "${PN}"

USERADD_PARAM:${PN} = "-u 5000 -d /persistent/config/confd-data -s /bin/false -g daemon confd"

# install the post-format script
SRC_URI:append = "file://data/01-init-confd.sh"
FILES:${PN} += "${sbindir}/init-overlays.d/format-hooks/01-init-confd.sh"

do_install:append () {
    install -d ${D}${sbindir}/init-overlays.d/format-hooks
    install -m 0744 ${WORKDIR}/data/01-init-confd.sh ${D}/${sbindir}/init-overlays.d/format-hooks
}
