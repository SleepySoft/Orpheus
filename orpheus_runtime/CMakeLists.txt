cmake_minimum_required(VERSION 3.16)
project(orpheus_runtime CXX)

add_library(orpheus_runtime_lib STATIC)

target_sources(orpheus_runtime_lib
    PRIVATE
        src/plan.cpp
        src/loader.cpp
        src/runtime.cpp
        src/wav_io.cpp
)

target_include_directories(orpheus_runtime_lib
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/orpheus_abi/include
        ${CMAKE_SOURCE_DIR}/third_party
)

target_link_libraries(orpheus_runtime_lib PUBLIC orpheus_abi ${CMAKE_DL_LIBS})

set_target_properties(orpheus_runtime_lib PROPERTIES
    CXX_STANDARD 11
    CXX_STANDARD_REQUIRED ON
)

add_executable(orpheus_runtime src/main.cpp)
target_link_libraries(orpheus_runtime PRIVATE orpheus_runtime_lib)
set_target_properties(orpheus_runtime PROPERTIES
    CXX_STANDARD 11
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

add_executable(orpheus_rt_host src/rt_host.cpp)
target_link_libraries(orpheus_rt_host PRIVATE orpheus_runtime_lib)
set_target_properties(orpheus_rt_host PROPERTIES
    CXX_STANDARD 11
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

# miniaudio needs some platform libs on Windows
if(WIN32)
    target_link_libraries(orpheus_rt_host PRIVATE ole32 oleaut32 uuid winmm)
elseif(APPLE)
    target_link_libraries(orpheus_rt_host PRIVATE
        "-framework CoreFoundation" "-framework CoreAudio" "-framework AudioToolbox" pthread m)
elseif(UNIX)
    target_link_libraries(orpheus_rt_host PRIVATE pthread m ${CMAKE_DL_LIBS})
endif()
