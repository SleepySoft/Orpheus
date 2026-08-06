#ifndef ORPHEUS_WAV_OUT_H
#define ORPHEUS_WAV_OUT_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char file_path[512];
    float* samples;        /* 采样缓冲（prepare 分配，destroy 落盘并释放） */
    uint32_t capacity;
    uint32_t count;
    uint32_t channels;
    uint32_t sample_rate;
} WavOutState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif
