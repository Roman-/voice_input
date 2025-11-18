cmake_minimum_required(VERSION 3.5)
project(AudioRecorder)

set(CMAKE_CXX_STANDARD 17)

# macOS configuration
# Try common Homebrew locations for Qt6
if(EXISTS "/opt/homebrew/opt/qt@6")
    set(CMAKE_PREFIX_PATH "${CMAKE_PREFIX_PATH};/opt/homebrew/opt/qt@6")
elseif(EXISTS "/usr/local/opt/qt@6")
    set(CMAKE_PREFIX_PATH "${CMAKE_PREFIX_PATH};/usr/local/opt/qt@6")
endif()

# Find Qt6
find_package(Qt6 COMPONENTS Core Gui Widgets Network Concurrent REQUIRED)

# Find PortAudio (Homebrew)
find_path(PORTAUDIO_INCLUDE_DIR portaudio.h PATHS /opt/homebrew/include /usr/local/include)
find_library(PORTAUDIO_LIBRARY portaudio PATHS /opt/homebrew/lib /usr/local/lib)

if(NOT PORTAUDIO_INCLUDE_DIR OR NOT PORTAUDIO_LIBRARY)
    message(FATAL_ERROR "PortAudio library not found. Please install: brew install portaudio")
endif()

# LAME is optional on macOS (we'll use WAV instead)
find_path(LAME_INCLUDE_DIR lame/lame.h PATHS /opt/homebrew/include /usr/local/include)
find_library(LAME_LIBRARY mp3lame PATHS /opt/homebrew/lib /usr/local/lib)

# Include directories
include_directories(
    ${PORTAUDIO_INCLUDE_DIR}
    src
    src/config
)

# Only add LAME include if found
if(LAME_INCLUDE_DIR)
    include_directories(${LAME_INCLUDE_DIR})
    add_definitions(-DLAME_INCLUDE_DIR)
    message(STATUS "LAME found: ${LAME_INCLUDE_DIR}, ${LAME_LIBRARY}")
else()
    message(WARNING "LAME not found - MP3 conversion will be disabled")
endif()

# Set automatic MOC, UIC, and RCC
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Source files
set(SOURCES
    main.cpp
    src/core/audiorecorder.cpp
    src/core/audioconverter.cpp
    src/core/formatutils.cpp
    src/core/openaitranscriptionservice.cpp
    src/core/statusutils.cpp
    src/ui/mainwindow.cpp
)

add_executable(romans_voice_input ${SOURCES})

target_link_libraries(romans_voice_input
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::Concurrent
    ${PORTAUDIO_LIBRARY}
)

# Link LAME if found (optional for WAV support)
if(LAME_LIBRARY)
    target_link_libraries(romans_voice_input ${LAME_LIBRARY})
endif()

# macOS bundle configuration
set_target_properties(romans_voice_input PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/mac/Info.plist"
)

# Link macOS frameworks for clipboard, accessibility, and hotkeys
target_link_libraries(romans_voice_input
    "-framework CoreGraphics"
    "-framework ApplicationServices"
    "-framework Carbon"
)