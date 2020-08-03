#!/bin/bash

rm -rf build
rm -rf out
RELEASE_PATH=libaudio_utils-lib-ios
# 删除旧文件
rm -rf $RELEASE_PATH

# 创建文件夹并拷贝源文件
mkdir -p $RELEASE_PATH

# 编译
sh cross_build.sh

# 合成静态库

#目标.a文件存放位置
RELEASE_LIB_PATH=$RELEASE_PATH/lib
mkdir -p $RELEASE_LIB_PATH

cp -r out/Release/build-ios/install/lib/libaudio_utils.a $RELEASE_LIB_PATH/libaudio_utils_arm.a
cp -r out/Release/build-ios-sim64/install/lib/libaudio_utils.a $RELEASE_LIB_PATH/libaudio_utils_sim64.a
cp -r out/Release/build-ios-sim/install/lib/libaudio_utils.a $RELEASE_LIB_PATH/libaudio_utils_sim.a

#拷贝头文件
cp -r out/Release/build-ios/install/include $RELEASE_PATH/include

cd $RELEASE_LIB_PATH
#合成
lipo -create -output libaudio_utils.a libaudio_utils_arm.a libaudio_utils_sim64.a libaudio_utils_sim.a
#删除中间产物
rm -rf libaudio_utils_arm.a
rm -rf libaudio_utils_sim64.a
rm -rf libaudio_utils_sim.a

