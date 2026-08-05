cmake_minimum_required(VERSION 3.16)

# 自动发现所有包含 CMakeLists.txt 的组件目录
file(GLOB_RECURSE component_cmake_files
    RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/*/CMakeLists.txt
)

foreach(cmake_file ${component_cmake_files})
    get_filename_component(comp_dir ${cmake_file} DIRECTORY)
    add_subdirectory(${comp_dir})
endforeach()
