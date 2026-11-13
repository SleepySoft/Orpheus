#ifndef ORPHEUS_TEST_PRINTF_HOOK_H
#define ORPHEUS_TEST_PRINTF_HOOK_H
#include <stddef.h>
#ifdef _MSC_VER
/* MSVC /std:c11：UCRT 的 stdio.h 要求先完成 snprintf 函数声明才允许宏映射，
   命令行 -Dsnprintf=xxx 直接冲突报 C1189。先撤下宏、声明后恢复，
   后续算法文件中的 snprintf 调用仍重定向到测试符号。 */
#pragma push_macro("snprintf")
#undef snprintf
#include <stdio.h>
#pragma pop_macro("snprintf")
#endif
/* MinGW的stdio头保护snprintf宏；注入函数须提前声明可变参数ABI。 */
#ifdef snprintf
int snprintf(char* buffer, size_t size, const char* format, ...);
#endif
#endif
