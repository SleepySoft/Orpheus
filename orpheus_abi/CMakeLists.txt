cmake_minimum_required(VERSION 3.16)
project(orpheus_abi C)

add_library(orpheus_abi INTERFACE)
target_include_directories(orpheus_abi INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
