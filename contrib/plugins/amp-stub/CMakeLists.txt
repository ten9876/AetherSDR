cmake_minimum_required(VERSION 3.16)
project(ampstub VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)

# Path to AetherSDR source root (override with -DAETHERSDR_ROOT=...)
if(NOT DEFINED AETHERSDR_ROOT)
    set(AETHERSDR_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../..")
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

add_library(ampstub SHARED
    AmpStubPlugin.h
    AmpStubPlugin.cpp
)

target_include_directories(ampstub PRIVATE
    "${AETHERSDR_ROOT}/src"
)

target_link_libraries(ampstub PRIVATE
    Qt6::Core
    Qt6::Widgets
)

# Install to user plugin directory
install(TARGETS ampstub
    LIBRARY DESTINATION "$ENV{HOME}/.config/AetherSDR/plugins/amp"
)
