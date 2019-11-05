

Linux  :
    cd lib-audio-utils
    ./run_test.sh

Android:
    Android Need [NDK r14b](https://dl.google.com/android/repository/android-ndk-r14b-linux-x86_64.zip)
    ./cross_build.sh
    ./package.sh
    cd android/lib-audio-utils/src/main/jni/
    ndk-build
    install the apk to android,now you can use the score on android

iOS   :
    iOS Need Xcode
    ./cross_build.sh
    ./package.sh
    cd ios/dubscore
    open the project you can use the score on ios
    the score.framework need ffmpeg,the ffmpeg lib in IJKMediaFramework. so you need import the IJKMediaFramework to project
