cmake_minimum_required(VERSION 3.10)
project(VoiceInputApp LANGUAGES CXX)

# Tell CMake we want to use C++17 (or higher).
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Include Cmake's automoc processing
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Find Qt components
find_package(Qt5 5.15 REQUIRED COMPONENTS Core Gui Widgets Multimedia Network)

# Source files
set(SOURCES
    main.cpp
    mainwindow.cpp
    mainwindow.h
    whisperapi.cpp
    whisperapi.h
)

# Create an executable from our source files
add_executable(${PROJECT_NAME} ${SOURCES})

# Link Qt libraries
target_link_libraries(${PROJECT_NAME}
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
    Qt5::Multimedia
    Qt5::Network
)

# If you have UI .ui files, use:
# set(UI_FILES mainwindow.ui)
# qt5_wrap_ui(UI_HEADERS ${UI_FILES})
# and add the generated UI_HEADERS to your list of sources.
