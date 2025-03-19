cmake_minimum_required(VERSION 3.5)
project(AudioRecorder)

set(CMAKE_CXX_STANDARD 17)

# Find Qt5 (adjust if you have Qt6)
find_package(Qt5 COMPONENTS Core Widgets Test Concurrent Network REQUIRED)

# Find PortAudio and libfdk-aac
find_package(PkgConfig REQUIRED)
pkg_check_modules(PORTAUDIO REQUIRED portaudio-2.0)
pkg_check_modules(FDK_AAC REQUIRED fdk-aac)

# Include directories
include_directories(
    ${PORTAUDIO_INCLUDE_DIRS}
    ${FDK_AAC_INCLUDE_DIRS}
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
    src/core/transcriptionservice.cpp
    src/ui/mainwindow.cpp
)

add_executable(audio_recorder ${SOURCES})

target_link_libraries(audio_recorder
    Qt5::Core
    Qt5::Widgets
    Qt5::Concurrent
    Qt5::Network
    ${PORTAUDIO_LIBRARIES}
    ${FDK_AAC_LIBRARIES}
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
    src/core/transcriptionservice.cpp
)

target_link_libraries(test_recorder
    Qt5::Core
    Qt5::Test
    Qt5::Concurrent
    Qt5::Network
    ${PORTAUDIO_LIBRARIES}
    ${FDK_AAC_LIBRARIES}
)

target_link_libraries(test_file_verification
    Qt5::Core
    Qt5::Test
    Qt5::Concurrent
    Qt5::Network
    ${PORTAUDIO_LIBRARIES}
    ${FDK_AAC_LIBRARIES}
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
