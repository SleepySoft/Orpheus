#ifndef ORPHEUS_SWEEP_RECORD_H
#define ORPHEUS_SWEEP_RECORD_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SWEEP_RECORD_MAX_BINS 256
#define SWEEP_RECORD_JSON_CAP 16384

/* 扫频记录：与 sweep_gen 使用相同参数，按当前频率把输入能量分箱累计，
   扫频结束后得到 频率→幅度 曲线（探针 JSON 供绘图控件消费）。 */
typedef struct {
    uint32_t channels;
    uint32_t bins;
    float start_freq;
    float end_freq;
    float duration_s;
    bool log_scale;
    double t;                         /* 已处理时长（秒） */
    uint64_t total_frames;            /* 已处理帧数（整数，用于精确判定完成） */
    uint64_t duration_frames;         /* 扫频总帧数 */
    float freq[SWEEP_RECORD_MAX_BINS];   /* 各箱中心频率 */
    float acc[SWEEP_RECORD_MAX_BINS];    /* 能量累计（通道 0） */
    uint32_t count[SWEEP_RECORD_MAX_BINS];
    float mag[SWEEP_RECORD_MAX_BINS];    /* 最终幅度（RMS） */
    float progress;                   /* 探针：0..1 */
    bool done;
    char json[SWEEP_RECORD_JSON_CAP]; /* get_param 非实时线程编码用 */
} SweepRecordState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif /* ORPHEUS_SWEEP_RECORD_H */
