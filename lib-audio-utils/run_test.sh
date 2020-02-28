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

echo -e "\033[1;43;30m\ntest_wav_dec...\033[0m"
./tests/test_wav_dec ../data/1582626292130.wav

echo -e "\033[1;43;30m\ntest_wav_crop...\033[0m"
./tests/test_wav_crop ../data/1582626292130.wav 1582626292130_crop.wav

echo -e "\033[1;43;30m\ntest_beautify...\033[0m"
./tests/test_beautify ../data/pcm_mono_44kHz_0035.pcm test_beautify.pcm

echo -e "\033[1;43;30m\ntest_noise_suppression...\033[0m"
./tests/test_noise_suppression ../data/pcm_mono_44kHz_0035.pcm test_noise_suppression.pcm

echo -e "\033[1;43;30m\ntest_volume_limiter...\033[0m"
./tests/test_volume_limiter ../data/pcm_mono_44kHz_0035.pcm test_volume_limiter.pcm

echo -e "\033[1;43;30m\ntest_reverb...\033[0m"
./tests/test_reverb ../data/pcm_mono_44kHz_0035.pcm 44100 1 test_reverb.pcm

echo -e "\033[1;43;30m\ntest_xm_audio_utils_fade...\033[0m"
./tests/test_xm_audio_utils_fade ../data/pcm_mono_44kHz_0035.pcm 44100 1 44100 2 utils_fade_pcm_mono_44kHz_0035.pcm

echo -e "\033[1;43;30m\ntest_xm_audio_utils_mix...\033[0m"
./tests/test_xm_audio_utils_mix ../data/effect_config.txt 44100 utils_mix_side_chain_test.pcm

echo -e "\033[1;43;30m\ntest_xm_audio_utils_effects...\033[0m"
./tests/test_xm_audio_utils_effects ../data/effect_config.txt 44100 1 utils_effect_mono_44kHz_0035.pcm

echo -e "\033[1;43;30m\ntest_xm_audio_generator_mix...\033[0m"
./tests/test_xm_audio_generator_mix ../data/effect_config.txt generator_pcm_mono_44kHz_0035_mix.pcm

echo -e "\033[1;43;30m\ntest_xm_audio_generator_effect...\033[0m"
./tests/test_xm_audio_generator_effect ../data/effect_config.txt generator_pcm_mono_44kHz_0035_effect.pcm

echo -e "\033[1;43;30m\ntest_xm_audio_generator...\033[0m"
./tests/test_xm_audio_generator ../data/effect_config.txt generator_pcm_mono_44kHz_0035.pcm
