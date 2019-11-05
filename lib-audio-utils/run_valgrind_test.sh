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

echo -e "\033[1;43;30m\ntest_echo...\033[0m"
valgrind --leak-check=full --log-file=valgrind_log/test_echo.log ./tests/test_effects ../data/pcm_mono_44kHz_0035.pcm test_echo.pcm echo 0.8 0.9 1000 0.3

echo -e "\033[1;43;30m\ntest_echos...\033[0m"
valgrind --leak-check=full --log-file=valgrind_log/test_echos.log ./tests/test_effects ../data/pcm_mono_44kHz_0035.pcm test_echos.pcm echos 0.8 0.7 700 0.25 700 0.3

echo -e "\033[1;43;30m\ntest_reverb...\033[0m"
valgrind --leak-check=full --log-file=valgrind_log/test_reverb.log ./tests/test_effects ../data/pcm_mono_44kHz_0035.pcm test_reverb.pcm reverb -w 50.0 50.0 100.0 0.0 5.0

cat valgrind_log/test_fifo.log
echo -e "\n"
cat valgrind_log/test_logger.log
echo -e "\n"
cat valgrind_log/test_echo.log
echo -e "\n"
cat valgrind_log/test_echos.log
echo -e "\n"
cat valgrind_log/test_reverb.log
echo -e "\n"
