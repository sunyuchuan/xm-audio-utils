#!/bin/bash -x

set -e -o pipefail

NPROC=$(grep -c ^processor /proc/cpuinfo)
ROOT_DIR=$PWD
BUILD_DIR=$ROOT_DIR/build

clean_up() {
    rm -rf $BUILD_DIR
    mkdir $BUILD_DIR
}

configure_lame() {
    emconfigure ./configure \
    --prefix=$BUILD_DIR \
    --host=x86_64 \
    --disable-shared \
    --disable-frontend \
    --disable-decoder \
    --enable-static
}

make_lame() {
    emmake make clean
    emmake make -j${NPROC}
    emmake make install
}

main() {
    clean_up
    configure_lame
    make_lame
}

main "$@"
