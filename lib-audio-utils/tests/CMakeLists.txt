
add_executable(test_fifo test_fifo.c)
target_link_libraries(test_fifo ${PROJECT_NAME})

add_executable(test_logger test_logger.c)
target_link_libraries(test_logger ${PROJECT_NAME} pthread)

add_executable(test_effects test_effects.c)
target_link_libraries(test_effects ${PROJECT_NAME} m pthread)

add_executable(test_noise_suppression test_noise_suppression.c)
target_link_libraries(test_noise_suppression ${PROJECT_NAME} m pthread)

add_executable(test_beautify test_beautify.c)
target_link_libraries(test_beautify ${PROJECT_NAME} m pthread)

add_executable(test_reverb test_reverb.c)
target_link_libraries(test_reverb ${PROJECT_NAME} m pthread)

add_executable(test_volume_limiter test_volume_limiter.c)
target_link_libraries(test_volume_limiter ${PROJECT_NAME} m pthread)

add_executable(test_xm_audio_utils_fade test_xm_audio_utils_fade.c)
target_link_libraries(test_xm_audio_utils_fade ${PROJECT_NAME} m pthread)

add_executable(test_xm_audio_utils_mix test_xm_audio_utils_mix.c)
target_link_libraries(test_xm_audio_utils_mix ${PROJECT_NAME} m pthread)

add_executable(test_xm_audio_utils_effects test_xm_audio_utils_effects.c)
target_link_libraries(test_xm_audio_utils_effects ${PROJECT_NAME} m pthread)

add_executable(test_xm_audio_generator test_xm_audio_generator.c)
target_link_libraries(test_xm_audio_generator ${PROJECT_NAME} m pthread)

add_executable(test_xm_audio_generator_mix test_xm_audio_generator_mix.c)
target_link_libraries(test_xm_audio_generator_mix ${PROJECT_NAME} m pthread)

add_executable(test_xm_audio_generator_effect test_xm_audio_generator_effect.c)
target_link_libraries(test_xm_audio_generator_effect ${PROJECT_NAME} m pthread)

add_executable(test_wav_dec test_wav_dec.c)
target_link_libraries(test_wav_dec ${PROJECT_NAME})

add_executable(test_wav_crop test_wav_crop.c)
target_link_libraries(test_wav_crop ${PROJECT_NAME})

add_executable(test_wav_concat test_wav_concat.c)
target_link_libraries(test_wav_concat ${PROJECT_NAME})
