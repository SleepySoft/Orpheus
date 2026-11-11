#ifndef ORPHEUS_TEST_PRINTF_HOOK_H
#define ORPHEUS_TEST_PRINTF_HOOK_H
#include <stddef.h>
/* MinGW的stdio头保护snprintf宏；注入函数须提前声明可变参数ABI。 */
#ifdef snprintf
int snprintf(char* buffer, size_t size, const char* format, ...);
#endif
#endif
