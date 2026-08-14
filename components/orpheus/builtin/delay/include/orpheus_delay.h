#ifndef ORPHEUS_DELAY_H
#define ORPHEUS_DELAY_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float* buffer;          /* 延迟线（prepare 分配，destroy 释放） */
    uint32_t delay_samples;
    uint32_t write_pos;
    uint32_t channels;
    uint32_t capacity;
    float mix;              /* 目标 mix（smoothed） */
    float mix_smoothed;     /* 当前滑动 mix，process 实际使用值 */
    float mix_coeff;        /* 一阶滑动系数 */
    float delay_ms;         /* 参数缓存（restart_required） */
} DelayState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif
