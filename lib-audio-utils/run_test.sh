#!/bin/bash
clear
rm -rf build
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Debug -DENABLE_TESTING=1 ..
make


echo -e "\033[1;43;30m\ntest_fifo...\033[0m"
./tests/test_fifo
echo -e "\033[1;43;30m\ntest_logger...\033[0m"
./tests/test_logger

echo -e "\033[1;43;30m\ntest_beautify...\033[0m"
./tests/test_beautify ../data/pcm_mono_44kHz_0035.pcm test_beautify.pcm

echo -e "\033[1;43;30m\ntest_noise_suppression...\033[0m"
./tests/test_noise_suppression ../data/pcm_mono_44kHz_0035.pcm test_noise_suppression.pcm

echo -e "\033[1;43;30m\ntest_volume_limiter...\033[0m"
./tests/test_volume_limiter ../data/pcm_mono_44kHz_0035.pcm test_volume_limiter.pcm

#echo -e "\033[1;43;30m\ntest_reverb...\033[0m"
#./tests/test_effects ../data/pcm_mono_44kHz_0035.pcm test_reverb.pcm reverb -w 50.0 50.0 100.0 0.0 5.0

echo -e "\033[1;43;30m\ntest_audio_decoder...\033[0m"
./tests/test_audio_decoder ../data/bgm1.mp3 bgm1.pcm 44100 2

echo -e "\033[1;43;30m\ntest_audio_decoder...\033[0m"
./tests/test_audio_decoder ../data/side_chain_test.wav side_chain_test.pcm 44100 1

echo -e "\033[1;43;30m\ntest_audio_encoder...\033[0m"
./tests/test_audio_encoder ../data/pcm_mono_44kHz_0035.pcm 44100 1 mono_0035_encoder.mp4

echo -e "\033[1;43;30m\ntest_xm_audio_effects...\033[0m"
./tests/test_xm_audio_effects ../data/pcm_mono_44kHz_0035.pcm 44100 1 ../data/effect_config.txt test_xm_audio_effects.pcm

echo -e "\033[1;43;30m\ntest_xm_audio_mixer...\033[0m"
./tests/test_xm_audio_mixer side_chain_test.pcm 44100 1 ../data/effect_config.txt test_xm_audio_mixer.mp4

echo -e "\033[1;43;30m\ntest_xm_audio_utils_decode...\033[0m"
./tests/test_xm_audio_utils_decode ../data/side_chain_music_test.wav side_chain_music_test.pcm 44100 2

echo -e "\033[1;43;30m\ntest_xm_audio_utils_mix...\033[0m"
./tests/test_xm_audio_utils_mix ../data/side_chain_test.pcm 44100 1 ../data/effect_config.txt utils_mix_side_chain_test.pcm

echo -e "\033[1;43;30m\ntest_xm_audio_utils_effects...\033[0m"
./tests/test_xm_audio_utils_effects ../data/pcm_mono_44kHz_0035.pcm 44100 1 ../data/effect_config.txt utils_effect_mono_44kHz_0035.pcm

echo -e "\033[1;43;30m\ntest_xm_audio_generator...\033[0m"
./tests/test_xm_audio_generator ../data/pcm_mono_44kHz_0035.pcm 44100 1 ../data/effect_config.txt generator_pcm_mono_44kHz_0035.m4a
