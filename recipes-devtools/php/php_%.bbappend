# FFI (C/C++ compat) implementation
DEPENDS += "libffi"
EXTRA_OECONF += "--with-ffi"

# support for additional php modules
PACKAGECONFIG[bcmath] = "--enable-bcmath,--disable-bcmath,"
PACKAGECONFIG[curl] = "--with-curl,--without-curl,curl"

# add bonus features
IMAGE_FEATURES:append = "opcache sqlite3 mbstring mbregex openssl zip bcmath curl"
IMAGE_FEATURES:remove = "mysql imap"

# we need these features also for native php installs (during building)
PACKAGECONFIG:class-native:append = "sqlite3 openssl ipv6 mbstring bcmath curl"

# install a php.ini for native (with OpenSSL CA bundle)
pkg_postinst[network] = "1"
pkg_postinst:php-native() {
    mkdir -p ../recipe-sysroot-native/usr/lib/php8
    cd ../recipe-sysroot-native/usr/lib/php8
    wget https://curl.haxx.se/ca/cacert.pem -O cacert.pem
    echo "openssl.cafile=\"$(pwd)/cacert.pem\""
}
