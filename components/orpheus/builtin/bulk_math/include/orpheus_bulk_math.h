#ifndef ORPHEUS_BULK_MATH_H
#define ORPHEUS_BULK_MATH_H
#include "orpheus_abi.h"
typedef struct { int channels,elements,operation; float a,b; bool owns_memory; } BulkMathState;
#endif
