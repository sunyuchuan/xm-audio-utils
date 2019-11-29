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

echo -e "\033[1;43;30m\ntest_xm_audio_effects...\033[0m"
valgrind --leak-check=full --log-file=valgrind_log/test_xm_audio_effects.log ./tests/test_xm_audio_effects ../data/pcm_mono_44kHz_0035.pcm 44100 1 ../data/effect_config.txt test_xm_audio_effects.pcm
echo -e "\033[1;43;30m\ntest_xm_audio_mixer...\033[0m"
valgrind --leak-check=full --log-file=valgrind_log/test_xm_audio_mixer.log ./tests/test_xm_audio_mixer ../data/side_chain_test.pcm 44100 1 44100 2 ../data/effect_config.txt mixer_side_chain_test.pcm
echo -e "\033[1;43;30m\ntest_xm_audio_utils_fade...\033[0m"
valgrind --leak-check=full --log-file=valgrind_log/test_xm_audio_utils_fade.log ./tests/test_xm_audio_utils_fade ../data/pcm_mono_44kHz_0035.pcm 44100 1 44100 2 utils_fade_pcm_mono_44kHz_0035.pcm
echo -e "\033[1;43;30m\ntest_xm_audio_utils_mix...\033[0m"
valgrind --leak-check=full --log-file=valgrind_log/test_xm_audio_utils_mix.log ./tests/test_xm_audio_utils_mix ../data/side_chain_test.pcm 44100 1 44100 2 ../data/effect_config.txt utils_mix_side_chain_test.pcm
echo -e "\033[1;43;30m\ntest_xm_audio_utils_effects...\033[0m"
valgrind --leak-check=full --log-file=valgrind_log/test_xm_audio_utils_effects.log ./tests/test_xm_audio_utils_effects ../data/pcm_mono_44kHz_0035.pcm 44100 1 ../data/effect_config.txt utils_effect_mono_44kHz_0035.pcm
echo -e "\033[1;43;30m\ntest_xm_audio_generator...\033[0m"
valgrind --leak-check=full --log-file=valgrind_log/test_xm_audio_generator.log ./tests/test_xm_audio_generator ../data/pcm_mono_44kHz_0035.pcm 44100 1 44100 2 ../data/effect_config.txt generator_pcm_mono_44kHz_0035.pcm

cat valgrind_log/test_fifo.log
echo -e "\n"
cat valgrind_log/test_logger.log
echo -e "\n"
cat valgrind_log/test_beautify.log
echo -e "\n"
cat valgrind_log/test_noise_suppression.log
echo -e "\n"
cat valgrind_log/test_volume_limiter.log
echo -e "\n"
cat valgrind_log/test_xm_audio_effects.log
echo -e "\n"
cat valgrind_log/test_xm_audio_mixer.log
echo -e "\n"
cat valgrind_log/test_xm_audio_utils_fade.log
echo -e "\n"
cat valgrind_log/test_xm_audio_utils_mix.log
echo -e "\n"
cat valgrind_log/test_xm_audio_utils_effects.log
echo -e "\n"
cat valgrind_log/test_xm_audio_generator.log
echo -e "\n"
