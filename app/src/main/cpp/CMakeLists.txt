cmake_minimum_required(VERSION 3.22.1)
project(tmb_native_port_lab LANGUAGES CXX)

add_library(tmb_native SHARED
    native-lib.cpp
    iso9660.cpp
    ps2_elf.cpp
)

target_compile_features(tmb_native PRIVATE cxx_std_17)
target_include_directories(tmb_native PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(tmb_native PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(tmb_native PRIVATE GLESv2 log)
