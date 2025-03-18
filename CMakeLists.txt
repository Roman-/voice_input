cmake_minimum_required(VERSION 3.5)
project(AudioRecorder)

set(CMAKE_CXX_STANDARD 17)

# Find Qt5 (adjust if you have Qt6)
find_package(Qt5 COMPONENTS Core Widgets Test REQUIRED)

# Find PortAudio
find_package(PortAudio REQUIRED)

# Include directories
include_directories(
    ${PORTAUDIO_INCLUDE_DIRS}
    src
    src/config
)

# Source files
set(SOURCES
    main.cpp
    src/core/audiorecorder.cpp
    src/ui/mainwindow.cpp
    src/tests/test_audiorecorder.cpp
)

add_executable(audio_recorder ${SOURCES})

target_link_libraries(audio_recorder
    Qt5::Core
    Qt5::Widgets
    Qt5::Test
    ${PORTAUDIO_LIBRARIES}
)

# Optionally create a custom command to run tests (if you'd like)
# add_custom_target(run_tests
#   COMMAND audio_recorder --test
#   DEPENDS audio_recorder
# )
