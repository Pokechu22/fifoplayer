cmake_minimum_required(VERSION 3.1.0)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

project(dffclient)

if(CMAKE_VERSION VERSION_LESS "3.7.0")
    set(CMAKE_INCLUDE_CURRENT_DIR ON)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CheckIncludeFiles)
check_include_files(endian.h HAVE_ENDIAN_H)

if(HAVE_ENDIAN_H)
	add_definitions("-DHAVE_ENDIAN_H")
endif(HAVE_ENDIAN_H)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)

add_executable(dffclient client.cpp command_info.cpp ../source/FifoDataFile.cpp ../source/BPMemory.cpp)
target_link_libraries(dffclient Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network)
