#!/bin/bash -x

set -e -o pipefail

NPROC=$(grep -c ^processor /proc/cpuinfo)
ROOT_DIR=$PWD
BUILD_DIR=$ROOT_DIR/build
EM_TOOLCHAIN_FILE=/home/sunyc/work/emscripten/emsdk/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake
PTHREAD_FLAGS='-s USE_PTHREADS=1'
export CFLAGS=$PTHREAD_FLAGS
export CPPFLAGS=$PTHREAD_FLAGS
export LDFLAGS=$PTHREAD_FLAGS

export COMMON_FF_CFG_FLAGS=
. $ROOT_DIR/module-lite.sh

clean_up() {
    rm -rf $BUILD_DIR
    mkdir $BUILD_DIR
}

delete_RANLIB() {
    sed -i '/RANLIB=llvm-ranlib/d' ffbuild/config.mak
}

configure_ffmpeg() {
    emconfigure ./configure \
    $COMMON_FF_CFG_FLAGS \
    --prefix=$BUILD_DIR \
    --extra-cflags="-I$BUILD_DIR/include" \
    --extra-cxxflags="-I$BUILD_DIR/include" \
    --extra-ldflags="-L$BUILD_DIR/lib" \
    --nm="llvm-nm -g" \
    --ar=emar \
    --as=llvm-as \
    --ranlib=llvm-ranlib \
    --cc=emcc \
    --cxx=em++ \
    --objcc=emcc \
    --dep-cc=emcc \
    --disable-asm \
    --enable-gpl \
    --enable-cross-compile \
    --target-os=none \
    --arch=x86_64 \
    --cpu=generic \
    --enable-static

    delete_RANLIB
}

make_ffmpeg() {
    emmake make clean
    emmake make -j${NPROC}
    emmake make install
}

build_ffmpegjs() {
    emcc \
    -I. -I./fftools -I$BUILD_DIR/include -L${BUILD_DIR}/lib \
    -Qunused-arguments -Oz \
    -o $2 fftools/ffmpeg_opt.c fftools/ffmpeg_filter.c fftools/ffmpeg_hw.c fftools/cmdutils.c fftools/ffmpeg.c \
    -lavfilter -lavformat -lavcodec -lswresample -lavutil \
    -Wno-deprecated-declarations -Wno-pointer-sign -Wno-implicit-int-float-conversion -Wno-switch -Wno-parentheses \
    -s USE_SDL=2 \
    $PTHREAD_FLAGS \
    -s INVOKE_RUN=0 \
    -s PTHREAD_POOL_SIZE=8 \
    -s PROXY_TO_PTHREAD=1 \
    -s SINGLE_FILE=$1 \
    -s EXPORTED_FUNCTIONS="[_main, _proxy_main]" \
    -s EXTRA_EXPORTED_RUNTIME_METHODS="[cwrap, FS, getValue, setValue]" \
    -s TOTAL_MEMORY=1065353216
}

main() {
    clean_up
    configure_ffmpeg
    make_ffmpeg
    # build_ffmpegjs 1 dist/ffmpeg-core.js
    # build_ffmpegjs 0 dist-wasm/ffmpeg-core.js
}

main "$@"
