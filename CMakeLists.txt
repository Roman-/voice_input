cmake_minimum_required(VERSION 3.5)
project(AudioRecorder)

set(CMAKE_CXX_STANDARD 17)

# Find Qt5 (adjust if you have Qt6)
find_package(Qt5 COMPONENTS Core Widgets Test Concurrent Network REQUIRED)

# Find PortAudio (using pkg-config)
find_package(PkgConfig REQUIRED)
pkg_check_modules(PORTAUDIO REQUIRED portaudio-2.0)

# For LAME MP3 library, we'll use a direct approach
find_path(LAME_INCLUDE_DIR lame/lame.h)
find_library(LAME_LIBRARY mp3lame)

if(NOT LAME_INCLUDE_DIR OR NOT LAME_LIBRARY)
    message(FATAL_ERROR "LAME library not found. Please install libmp3lame-dev.")
endif()

# Include directories
include_directories(
    ${PORTAUDIO_INCLUDE_DIRS}
    ${LAME_INCLUDE_DIR}
    src
    src/config
)

# Set automatic MOC, UIC, and RCC
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Source files
set(SOURCES
    main.cpp
    src/core/audiorecorder.cpp
    src/core/openaitranscriptionservice.cpp
    src/core/statusutils.cpp
    src/ui/mainwindow.cpp
)

add_executable(audio_recorder ${SOURCES})

target_link_libraries(audio_recorder
    Qt5::Core
    Qt5::Widgets
    Qt5::Concurrent
    Qt5::Network
    ${PORTAUDIO_LIBRARIES}
    ${LAME_LIBRARY}
)

# Add test executables
add_executable(test_recorder
    src/tests/test_audiorecorder.cpp
    src/core/audiorecorder.cpp
)

# Add file verification test
add_executable(test_file_verification
    src/tests/test_file_verification.cpp
    src/core/audiorecorder.cpp
)

# Add transcription test
add_executable(test_transcription
    src/tests/test_transcription.cpp
    src/core/openaitranscriptionservice.cpp
)

target_link_libraries(test_recorder
    Qt5::Core
    Qt5::Test
    Qt5::Concurrent
    Qt5::Network
    ${PORTAUDIO_LIBRARIES}
    ${LAME_LIBRARY}
)

target_link_libraries(test_file_verification
    Qt5::Core
    Qt5::Test
    Qt5::Concurrent
    Qt5::Network
    ${PORTAUDIO_LIBRARIES}
    ${LAME_LIBRARY}
)

# No mock mode needed, using real API

target_link_libraries(test_transcription
    Qt5::Core
    Qt5::Test
    Qt5::Network
)

# Optionally create a custom command to run tests (if you'd like)
# add_custom_target(run_tests
#   COMMAND audio_recorder --test
#   DEPENDS audio_recorder
# )
