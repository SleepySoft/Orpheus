cmake_minimum_required(VERSION 3.16)
project(orpheus_builtin_rfft C)
set(COMPONENT_NAME orpheus_builtin_rfft)
include("${CMAKE_CURRENT_LIST_DIR}/generated/component_sources.cmake")
add_library(${COMPONENT_NAME} SHARED ${ORPHEUS_COMPONENT_SOURCES})
target_include_directories(${COMPONENT_NAME} PUBLIC ${ORPHEUS_COMPONENT_INCLUDE_DIRS})
if(TARGET orpheus_abi)
    target_link_libraries(${COMPONENT_NAME} PUBLIC orpheus_abi)
else()
    set(ORPHEUS_ABI_INCLUDE_DIR "" CACHE PATH "orpheus_abi.h 所在目录")
    if(NOT EXISTS "${ORPHEUS_ABI_INCLUDE_DIR}/orpheus_abi.h")
        message(FATAL_ERROR "请设置 ORPHEUS_ABI_INCLUDE_DIR")
    endif()
    target_include_directories(${COMPONENT_NAME} PUBLIC "${ORPHEUS_ABI_INCLUDE_DIR}")
endif()
if(MSVC)
    target_compile_options(${COMPONENT_NAME} PRIVATE /utf-8)
endif()
if(UNIX)
    target_link_libraries(${COMPONENT_NAME} PRIVATE m)
endif()

target_compile_definitions(${COMPONENT_NAME}
    PRIVATE
        ORPHEUS_BUILDING_COMPONENT
)

set_target_properties(${COMPONENT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/components
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/components
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/components
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
)
