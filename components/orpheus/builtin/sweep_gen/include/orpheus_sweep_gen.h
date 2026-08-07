#ifndef ORPHEUS_SWEEP_GEN_H
#define ORPHEUS_SWEEP_GEN_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t channels;
    uint32_t sample_rate;  /* 参数：时钟源声明，成为图采样率 */
    double start_freq;
    double end_freq;
    double duration_s;
    double amplitude;
    bool log_scale;
    double t;     /* 已生成时长（秒） */
    double phase; /* 累积相位（弧度） */
    float progress;     /* 探针：0..1 */
    float current_freq; /* 探针：当前输出频率（扫完为 0） */
} SweepGenState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif
