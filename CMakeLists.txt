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

add_executable(romans_voice_input ${SOURCES})

target_link_libraries(romans_voice_input
    Qt5::Core
    Qt5::Widgets
    Qt5::Concurrent
    Qt5::Network
    ${PORTAUDIO_LIBRARIES}
    ${LAME_LIBRARY}
)