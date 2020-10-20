#!/bin/bash -x

set -e -o pipefail

NPROC=$(grep -c ^processor /proc/cpuinfo)
ROOT_DIR=$PWD
BUILD_DIR=$ROOT_DIR/build
FFMPEG_BUILD_DIR=$ROOT_DIR/../ffmpeg-3.4.7/build
LAME_BUILD_DIR=$ROOT_DIR/../lame-3.100/build
EM_TOOLCHAIN_FILE=/home/sunyc/work/emscripten/emsdk/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake
PTHREAD_FLAGS='-s USE_PTHREADS=0'
export CFLAGS=$PTHREAD_FLAGS
export CPPFLAGS=$PTHREAD_FLAGS
export LDFLAGS=$PTHREAD_FLAGS

clean_up() {
    rm -rf $BUILD_DIR
    mkdir $BUILD_DIR
    mkdir $BUILD_DIR/dist-wasm
}

audio_utils_make() {
    cd $BUILD_DIR
    emmake cmake .. \
        -DCMAKE_INSTALL_PREFIX=${BUILD_DIR} \
        -DCMAKE_TOOLCHAIN_FILE=${EM_TOOLCHAIN_FILE} \
        -DBUILD_SHARED_LIBS=OFF
    emmake make clean
    emmake make -j${NPROC}
    emmake make install
    cd $ROOT_DIR
}

build_audio_utils_js() {
    emcc \
    -I. -I${FFMPEG_BUILD_DIR}/include -I${BUILD_DIR}/install/include \
    -I${LAME_BUILD_DIR}/include -L${LAME_BUILD_DIR}/lib \
    -L${FFMPEG_BUILD_DIR}/lib -L${BUILD_DIR}/install/lib \
    -I./src -I./src/effects \
    -Qunused-arguments -O2 \
    -o $2 src/tools/log.c src/xm_wav_utils.c src/xm_duration_parser.c src/xm_audio_utils.c src/xm_audio_generator.c src/xm_audio_transcode.c \
    -laudio_utils -lavfilter -lavformat -lavcodec -lswresample -lavutil -llibmp3lame \
    -Wno-deprecated-declarations -Wno-pointer-sign -Wno-implicit-int-float-conversion -Wno-switch -Wno-parentheses \
    -s USE_SDL=2 \
    $PTHREAD_FLAGS \
    -s INVOKE_RUN=0 \
    -s SINGLE_FILE=$1 \
    -s EXTRA_EXPORTED_RUNTIME_METHODS="[ccall, cwrap, FS]" \
    -s TOTAL_MEMORY=67108864 \
    -s ALLOW_MEMORY_GROWTH=1
}

main() {
    clean_up
    audio_utils_make
    build_audio_utils_js 0 $BUILD_DIR/dist-wasm/audio_utils.js
    #build_audio_utils_js 1 $BUILD_DIR/dist/audio_utils.js
}

main "$@"
