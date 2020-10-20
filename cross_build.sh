#!/usr/bin/env bash

#--------------------
set -e
set +x

UNAME_S=$(uname -s)
UNAME_SM=$(uname -sm)
echo "build on $UNAME_SM"

ROOT_DIR=$PWD
LAME_DIR=$ROOT_DIR/lame-3.100
FFMPEG_DIR=$ROOT_DIR/ffmpeg-3.4.7
AUDIO_UTILS_DIR=$ROOT_DIR/lib-audio-utils
AUDIO_UTILS_BUILD_DIR=$ROOT_DIR/lib-audio-utils/build/dist-wasm
RELEASE_DIR=$ROOT_DIR/out

###################### web  ##########################
build_wasm() {
    ##### build libmp3lame
    cd $LAME_DIR
    bash build_js.sh

    ##### build ffmpeg
    cd $FFMPEG_DIR
    bash build_js.sh

    ##### build audio-utils
    cd $AUDIO_UTILS_DIR
    bash build_js.sh

    ##### copy wasm file to out
    cd $ROOT_DIR
    rm -rf $RELEASE_DIR
    mkdir $RELEASE_DIR
    cp $AUDIO_UTILS_BUILD_DIR/* $RELEASE_DIR
}

build_wrap() {
    case "$UNAME_S" in
        Linux)
            build_wasm $1
        ;;
    esac
}

echo_usage() {
    echo "Usage:"
    echo "  cross_build.sh Debug/Release"
    echo "      on linux build android only"
    echo "      on mac can build android and ios"
    echo "  cross_build.sh clean"
    echo "      clean compile file"
    echo "  cross_build.sh help"
    echo "      display this"
    exit 1
}

TARGET=$1
case $TARGET in
    clean)
        echo "remove out folder"
        rm -rf out
    ;;
    help)
        echo_usage
    ;;
    Debug|Release)
        echo "build start"
        build_wrap $1
        echo "build end"
    ;;
    *)
        echo "build start"
        build_wrap Release
        echo "build end"
    ;;
esac
