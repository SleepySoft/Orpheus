cmake_minimum_required(VERSION 3.16)

# 基础版本测试目录：先建立空壳，后续填充 GoogleTest 用例
enable_testing()

# 占位测试：验证 ABI header 可编译
add_executable(abi_smoke abi_smoke.c)
target_include_directories(abi_smoke PRIVATE ${CMAKE_SOURCE_DIR}/orpheus_abi/include)
add_test(NAME abi_smoke COMMAND abi_smoke)

# OLINK 帧级 CLI（供 pytest 跨语言互测驱动）
add_executable(olink_cli olink_cli.c)
target_link_libraries(olink_cli PRIVATE orpheus_olink)
