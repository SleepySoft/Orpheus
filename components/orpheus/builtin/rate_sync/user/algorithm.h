#pragma once

#include <stdint.h>

typedef struct orpheus_builtin_rate_sync_AlgorithmState {
    float* fifo[2];        /* 每路 FIFO：capacity = align + 块长，容纳跨调用余量 */
    uint32_t wpos[2];      /* 写位置（帧，取模 capacity） */
    uint32_t rpos[2];      /* 读位置（帧，取模 capacity） */
    uint32_t stored[2];    /* 当前缓冲帧数 */
    uint32_t align;        /* 对齐窗口（每次输出帧数上界） */
    uint32_t capacity;     /* 每路 FIFO 容量（帧） */
} orpheus_builtin_rate_sync_AlgorithmState;
