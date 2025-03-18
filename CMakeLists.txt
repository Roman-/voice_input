cmake_minimum_required(VERSION 3.5)
project(AudioRecorder)

set(CMAKE_CXX_STANDARD 17)

# Find Qt5 (adjust if you have Qt6)
find_package(Qt5 COMPONENTS Core Widgets Test REQUIRED)

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
    src/ui/mainwindow.cpp
)

add_executable(audio_recorder ${SOURCES})

target_link_libraries(audio_recorder
    Qt5::Core
    Qt5::Widgets
    ${PORTAUDIO_LIBRARIES}
    ${FDK_AAC_LIBRARIES}
)

# Add test executable
add_executable(test_runner
    src/tests/test_audiorecorder.cpp
    src/core/audiorecorder.cpp
)

target_link_libraries(test_runner
    Qt5::Core
    Qt5::Test
    ${PORTAUDIO_LIBRARIES}
    ${FDK_AAC_LIBRARIES}
)

# Optionally create a custom command to run tests (if you'd like)
# add_custom_target(run_tests
#   COMMAND audio_recorder --test
#   DEPENDS audio_recorder
# )
