cmake_minimum_required(VERSION 3.16)
project(orpheus_abi C)

add_library(orpheus_abi INTERFACE)
target_include_directories(orpheus_abi INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# OLINK 成帧层（COBS + CRC16）：静态库，供宿主/测试链接；生成工程直接复制源码
add_library(orpheus_olink STATIC src/olink.c)
target_include_directories(orpheus_olink PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_library(orpheus_bridge_endpoint STATIC src/bridge_endpoint.c src/bridge_stdio.c)
target_include_directories(orpheus_bridge_endpoint PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(orpheus_bridge_endpoint PUBLIC orpheus_abi)
