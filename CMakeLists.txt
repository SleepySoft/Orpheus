cmake_minimum_required(VERSION 3.16)
project(Orpheus VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# MSVC: source files are UTF-8 (Chinese comments/strings); enable exceptions.
if(MSVC)
    add_compile_options(/utf-8 /EHsc)
endif()

# Runtime loads components as "lib<name>.dll" on Windows; MinGW already
# prefixes DLLs with "lib", MSVC does not. Unify the prefix for both.
if(WIN32)
    set(CMAKE_SHARED_LIBRARY_PREFIX "lib")
endif()

option(ORPHEUS_BUILD_RUNTIME "Build Orpheus Runtime" ON)
option(ORPHEUS_BUILD_COMPONENTS "Build built-in components" ON)
option(ORPHEUS_BUILD_TESTS "Build tests" ON)
option(ORPHEUS_BUILD_UI "Build UI" OFF)

# 平台检测
if(WIN32)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
endif()

add_subdirectory(orpheus_abi)

if(ORPHEUS_BUILD_RUNTIME)
    add_subdirectory(orpheus_runtime)
endif()

if(ORPHEUS_BUILD_COMPONENTS)
    add_subdirectory(components)
endif()

if(ORPHEUS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
