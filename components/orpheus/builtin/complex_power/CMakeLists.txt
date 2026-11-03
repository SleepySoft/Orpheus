cmake_minimum_required(VERSION 3.16)
set(COMPONENT_NAME orpheus_builtin_complex_power)
add_library(${COMPONENT_NAME} SHARED src/complex_power.c)
target_include_directories(${COMPONENT_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/orpheus_abi/include)
target_compile_definitions(${COMPONENT_NAME} PRIVATE ORPHEUS_BUILDING_COMPONENT)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${COMPONENT_NAME} PRIVATE -Werror=incompatible-pointer-types)
endif()
set_target_properties(${COMPONENT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/components
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/components
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/components
    C_STANDARD 11 C_STANDARD_REQUIRED ON)
