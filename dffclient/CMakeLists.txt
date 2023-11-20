cmake_minimum_required(VERSION 3.1.0)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

project(dffclient)

if(CMAKE_VERSION VERSION_LESS "3.7.0")
    set(CMAKE_INCLUDE_CURRENT_DIR ON)
endif()

include(CheckIncludeFiles)
check_include_files(endian.h HAVE_ENDIAN_H)

set(CMAKE_CXX_FLAGS "-std=c++20")
if(HAVE_ENDIAN_H)
	add_definitions("-DHAVE_ENDIAN_H")
endif(HAVE_ENDIAN_H)

find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets Network)

add_executable(dffclient client.cpp command_info.cpp ../source/FifoDataFile.cpp ../source/BPMemory.cpp)
target_link_libraries(dffclient Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Network)
