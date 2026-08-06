cmake_minimum_required(VERSION 3.16)

set(COMPONENT_NAME orpheus_builtin_resample)

add_library(${COMPONENT_NAME} SHARED)

target_sources(${COMPONENT_NAME}
    PRIVATE
        src/resample.c
)

target_include_directories(${COMPONENT_NAME}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/orpheus_abi/include
)

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
