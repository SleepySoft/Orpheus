#ifndef ORPHEUS_RATE_SYNC_H
#define ORPHEUS_RATE_SYNC_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RATE_SYNC_MAX_INPUTS 2
#define RATE_SYNC_MAX_BUFFER 131072

typedef struct {
    uint32_t channels;        /* 通道数 */
    uint32_t mode;            /* 0=auto(lcm), 1=fixed */
    uint32_t buffer_length;   /* fixed 手动值；0=auto */
    uint32_t align;           /* 对齐窗口（每路次输出帧数） */
    float* fifo[2];           /* per-input buffer: channels * align */
    uint32_t wpos[2];         /* write position (frames) */
    uint32_t rpos[2];         /* read position (frames) */
    uint32_t stored[2];       /* frames currently buffered per input */
    uint32_t init_block[2];   /* observed per-input, auto-detect */
} RateSyncState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif /* ORPHEUS_RATE_SYNC_H */
