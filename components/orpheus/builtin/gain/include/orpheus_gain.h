#ifndef ORPHEUS_GAIN_H
#define ORPHEUS_GAIN_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 公开状态结构体：统一内存拼接（生成路径按类型拼接、动态路径按 state_size 切片）。
   布局即契约：数组一律固定上限，注册槽偏移相对本结构体基址。 */
typedef struct {
    float gain_db;           /* 调音参数（注册槽，单位 dB） */
    float gain_linear;       /* 当前线性增益（平滑中） */
    float target_linear;     /* 目标线性增益 */
    float smoothing_coeff;
    float smoothing_ms;      /* 调音参数（注册槽，restart_required） */
    uint32_t channels;
} GainState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif /* ORPHEUS_GAIN_H */
