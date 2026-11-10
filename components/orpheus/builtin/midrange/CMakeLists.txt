cmake_minimum_required(VERSION 3.16)

set(COMPONENT_NAME orpheus_builtin_midrange)

include("${CMAKE_CURRENT_LIST_DIR}/generated/component_sources.cmake")
add_library(${COMPONENT_NAME} SHARED)

target_sources(${COMPONENT_NAME}
    PRIVATE
        ${ORPHEUS_COMPONENT_SOURCES}
)

target_include_directories(${COMPONENT_NAME}
    PUBLIC
        ${ORPHEUS_COMPONENT_INCLUDE_DIRS}
        ${CMAKE_SOURCE_DIR}/orpheus_abi/include
)
if(MSVC)
    target_compile_options(${COMPONENT_NAME} PRIVATE /utf-8)
elseif(NOT WIN32)
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
