# CMake Options
cmake_minimum_required(VERSION 3.5.1)
if(POLICY CMP0025)
  cmake_policy(SET CMP0025 NEW)
endif()
if(POLICY CMP0048)
  cmake_policy(SET CMP0048 NEW)
endif()
if(POLICY CMP0054)
  cmake_policy(SET CMP0054 NEW)
endif()

project(audio_utils C)

add_definitions(-D_GNU_SOURCE -D__STDC_FORMAT_MACROS -D__STDC_LIMIT_MACROS -D__STDC_CONSTANT_MACROS -D_POSIX_C_SOURCE=200809L)

set(CMAKE_C_EXTENSIONS      ON)
set(CMAKE_C_STANDARD        99)
if(${CMAKE_C_COMPILER_ID} STREQUAL "MSVC")
elseif(${CMAKE_C_COMPILER_ID} MATCHES "^(GNU|.*Clang)$")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror=implicit -std=gnu99 -fPIC")

    if(${CMAKE_C_COMPILER_ID} STREQUAL "GNU" AND ${CMAKE_C_COMPILER_VERSION} VERSION_GREATER "6.0.0")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wnarrowing -Werror=discarded-qualifiers -Werror=incompatible-pointer-types")
    endif()

    if(${CMAKE_C_COMPILER_ID} STREQUAL "Clang" OR ${CMAKE_C_COMPILER_ID} STREQUAL "AppleClang")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wnarrowing -Werror=ignored-qualifiers -Werror=incompatible-pointer-types")
    endif()

    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -fomit-frame-pointer")
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -fomit-frame-pointer -s")

    message(STATUS "gcc/clang cflags ${CMAKE_C_FLAGS}")
    message(STATUS "gcc/clang cflags debug ${CMAKE_C_FLAGS_DEBUG}")
    message(STATUS "gcc/clang cflags release ${CMAKE_C_FLAGS_RELEASE}")
endif()

message(STATUS "CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")

include_directories(${PROJECT_SOURCE_DIR}/include)
include_directories(${PROJECT_SOURCE_DIR}/src)
include_directories(${PROJECT_SOURCE_DIR}/src/effects)

# ffmpeg
if(${CMAKE_SYSTEM_NAME} STREQUAL "IOS")
    set(FFmpeg_INCLUDE_DIRS ${PROJECT_SOURCE_DIR}/../prebuilt/ios/)
    include_directories(${FFmpeg_INCLUDE_DIRS})
else()
    # include system lib
    include_directories("/usr/local/include")
    link_directories("/usr/local/lib")
endif()

add_library(${PROJECT_NAME}
src/tools/avstring.c
src/tools/conversion.c
src/tools/dict.c
src/tools/fifo.c
src/tools/log.c
src/tools/mem.c
src/tools/sdl_mutex.c
src/tools/util.c

src/effects/echo.c
src/effects/echos.c
src/effects/reverb.c

src/effects/dsp_tools/fft/fft8g.c
src/effects/dsp_tools/iir_design/iir_design.c
src/effects/beautify/compressor.c
src/effects/beautify/equalizer.c
src/effects/beautify/flanger.c
src/effects/beautify/limiter.c
src/effects/beautify/multiband_compressor.c
src/effects/beautify.c

src/effects/math/junior_func.c
src/effects/noise_suppression/noise_estimation.c
src/effects/noise_suppression.c

src/effects/volume_limiter.c

src/json/cJSON.c
src/json/json_parse.c

src/mixer_effects/fade_in_out.c
src/mixer_effects/side_chain_compress.c

src/effects/voice_effect.c
src/xm_audio_effects.c
src/xm_audio_mixer.c
src/xm_audio_utils.c
src/xm_audio_generator.c
)

# set prefix as global var
set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/install")

install(DIRECTORY include DESTINATION .)

install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    )

target_include_directories(${PROJECT_NAME}
                           PRIVATE src
                           PUBLIC include)

if(ENABLE_TESTING AND NOT ${CMAKE_SYSTEM_NAME} STREQUAL "Android" AND NOT IOS)
  add_subdirectory(tests)
endif()
