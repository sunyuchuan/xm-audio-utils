#!/bin/bash
clear
rm -rf build
mkdir -p build/valgrind_log
cd build
cmake -DCMAKE_BUILD_TYPE=Debug -DENABLE_TESTING=1 ..
make

echo -e "\033[1;43;30m\ntest_fifo...\033[0m"
valgrind --leak-check=full --log-file=valgrind_log/test_fifo.log ./tests/test_fifo
echo -e "\033[1;43;30m\ntest_logger...\033[0m"
valgrind --leak-check=full --log-file=valgrind_log/test_logger.log ./tests/test_logger
echo -e "\033[1;43;30m\ntest_beautify...\033[0m"
valgrind --leak-check=full --log-file=valgrind_log/test_beautify.log ./tests/test_beautify ../data/pcm_mono_44kHz_0035.pcm test_beautify.pcm
echo -e "\033[1;43;30m\ntest_noise_suppressionr...\033[0m"
valgrind --leak-check=full --log-file=valgrind_log/test_noise_suppression.log ./tests/test_noise_suppression ../data/pcm_mono_44kHz_0035.pcm test_noise_suppression.pcm
echo -e "\033[1;43;30m\ntest_volume_limiter...\033[0m"
valgrind --leak-check=full --log-file=valgrind_log/test_volume_limiter.log ./tests/test_volume_limiter ../data/pcm_mono_44kHz_0035.pcm test_volume_limiter.pcm

echo -e "\033[1;43;30m\ntest_audio_decoder_1...\033[0m"
valgrind --leak-check=full --log-file=valgrind_log/test_audio_decoder_1.log ./tests/test_audio_decoder ../data/bgm1.mp3 bgm1.pcm 44100 2
echo -e "\033[1;43;30m\ntest_audio_decoder_2...\033[0m"
valgrind --leak-check=full --log-file=valgrind_log/test_audio_decoder_2.log ./tests/test_audio_decoder ../data/side_chain_test.wav side_chain_test.pcm 44100 1
echo -e "\033[1;43;30m\ntest_audio_encoder...\033[0m"
valgrind --leak-check=full --log-file=valgrind_log/test_audio_encoder.log ./tests/test_audio_encoder ../data/pcm_mono_44kHz_0035.pcm 44100 1 mono_0035_encoder.mp4

echo -e "\033[1;43;30m\ntest_xm_audio_effects...\033[0m"
valgrind --leak-check=full --log-file=valgrind_log/test_xm_audio_effects.log ./tests/test_xm_audio_effects ../data/pcm_mono_44kHz_0035.pcm 44100 1 ../data/effect_config.txt test_xm_audio_effects.pcm
echo -e "\033[1;43;30m\ntest_xm_audio_mixer...\033[0m"
valgrind --leak-check=full --log-file=valgrind_log/test_xm_audio_mixer.log ./tests/test_xm_audio_mixer side_chain_test.pcm 44100 1 ../data/effect_config.txt test_xm_audio_mixer.mp4
echo -e "\033[1;43;30m\ntest_xm_audio_utils_decode...\033[0m"
valgrind --leak-check=full --log-file=valgrind_log/test_xm_audio_utils_decode.log ./tests/test_xm_audio_utils_decode ../data/side_chain_music_test.wav side_chain_music_test.pcm 44100 2
echo -e "\033[1;43;30m\ntest_xm_audio_utils_mix...\033[0m"
valgrind --leak-check=full --log-file=valgrind_log/test_xm_audio_utils_mix.log ./tests/test_xm_audio_utils_mix ../data/side_chain_test.pcm 44100 1 ../data/effect_config.txt utils_mix_side_chain_test.pcm
echo -e "\033[1;43;30m\ntest_xm_audio_utils_effects...\033[0m"
valgrind --leak-check=full --log-file=valgrind_log/test_xm_audio_utils_effects.log ./tests/test_xm_audio_utils_effects ../data/pcm_mono_44kHz_0035.pcm 44100 1 ../data/effect_config.txt utils_effect_mono_44kHz_0035.pcm
echo -e "\033[1;43;30m\ntest_xm_audio_generator...\033[0m"
valgrind --leak-check=full --log-file=valgrind_log/test_xm_audio_generator.log ./tests/test_xm_audio_generator ../data/pcm_mono_44kHz_0035.pcm 44100 1 ../data/effect_config.txt generator_pcm_mono_44kHz_0035.m4a

cat valgrind_log/test_fifo.log
echo -e "\n"
cat valgrind_log/test_logger.log
echo -e "\n"
cat valgrind_log/test_beautify.log
echo -e "\n"
cat valgrind_log/test_noise_suppression.log
echo -e "\n"
cat valgrind_log/test_volume_limiter.log

cat valgrind_log/test_audio_decoder_1.log
echo -e "\n"
cat valgrind_log/test_audio_decoder_2.log
echo -e "\n"
cat valgrind_log/test_audio_encoder.log

echo -e "\n"
cat valgrind_log/test_xm_audio_effects.log
echo -e "\n"
cat valgrind_log/test_xm_audio_mixer.log
echo -e "\n"
cat valgrind_log/test_xm_audio_utils_decode.log
echo -e "\n"
cat valgrind_log/test_xm_audio_utils_mix.log
echo -e "\n"
cat valgrind_log/test_xm_audio_utils_effects.log
echo -e "\n"
cat valgrind_log/test_xm_audio_generator.log
echo -e "\n"
