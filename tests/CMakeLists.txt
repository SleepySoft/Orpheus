cmake_minimum_required(VERSION 3.16)

# 基础版本测试目录：先建立空壳，后续填充 GoogleTest 用例
enable_testing()

# 占位测试：验证 ABI header 可编译
add_executable(abi_smoke abi_smoke.c)
target_include_directories(abi_smoke PRIVATE ${CMAKE_SOURCE_DIR}/orpheus_abi/include)
add_test(NAME abi_smoke COMMAND abi_smoke)

add_executable(loader_smoke loader_smoke.cpp)
target_link_libraries(loader_smoke PRIVATE orpheus_runtime_lib)
add_test(NAME loader_smoke COMMAND loader_smoke $<TARGET_FILE:orpheus_builtin_gain>)

add_library(timeline_probe_component SHARED timeline_probe_component.c)
target_include_directories(timeline_probe_component PRIVATE
	${CMAKE_SOURCE_DIR}/orpheus_abi/include
)
target_compile_definitions(timeline_probe_component PRIVATE ORPHEUS_BUILDING_COMPONENT)
set_target_properties(timeline_probe_component PROPERTIES
	OUTPUT_NAME orpheus_test_timeline_probe
	PREFIX lib
	RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests/timeline_components
	LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests/timeline_components
)

add_executable(timeline_runtime_smoke timeline_runtime_smoke.cpp)
target_link_libraries(timeline_runtime_smoke PRIVATE orpheus_runtime_lib)
add_test(NAME timeline_runtime_smoke
	COMMAND timeline_runtime_smoke $<TARGET_FILE_DIR:timeline_probe_component>
)

add_executable(rnc_mimo_nlms_smoke
	rnc_mimo_nlms_smoke.c
	${CMAKE_SOURCE_DIR}/components/orpheus/builtin/rnc_mimo_nlms/src/rnc_mimo_nlms.c
)
target_include_directories(rnc_mimo_nlms_smoke PRIVATE
	${CMAKE_SOURCE_DIR}/orpheus_abi/include
	${CMAKE_SOURCE_DIR}/components/orpheus/builtin/rnc_mimo_nlms/include
)
target_compile_definitions(rnc_mimo_nlms_smoke PRIVATE ORPHEUS_BUILDING_COMPONENT)
add_test(NAME rnc_mimo_nlms_smoke COMMAND rnc_mimo_nlms_smoke)

add_executable(baf_soft_clipper_smoke
	baf_soft_clipper_smoke.c
	${CMAKE_SOURCE_DIR}/components/orpheus/builtin/baf_soft_clipper/src/baf_soft_clipper.c
)
target_include_directories(baf_soft_clipper_smoke PRIVATE
	${CMAKE_SOURCE_DIR}/orpheus_abi/include
	${CMAKE_SOURCE_DIR}/components/orpheus/builtin/baf_soft_clipper/include
)
target_compile_definitions(baf_soft_clipper_smoke PRIVATE ORPHEUS_BUILDING_COMPONENT)
add_test(NAME baf_soft_clipper_smoke COMMAND baf_soft_clipper_smoke)

# OLINK 帧级 CLI（供 pytest 跨语言互测驱动）
add_executable(olink_cli olink_cli.c)
target_link_libraries(olink_cli PRIVATE orpheus_olink)
