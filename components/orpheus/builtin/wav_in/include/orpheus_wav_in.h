#ifndef ORPHEUS_WAV_IN_H
#define ORPHEUS_WAV_IN_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char file_path[512];
    float* samples;        /* 音频数据（prepare 分配，destroy 释放） */
    uint32_t total_frames;
    uint32_t position;
    uint32_t channels;
} WavInState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif
