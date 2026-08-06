#ifndef ORPHEUS_PROBE_WAVEFORM_H
#define ORPHEUS_PROBE_WAVEFORM_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PROBE_WAVEFORM_SAMPLES 1024 /* 环形缓冲容量（帧，取第 0 通道） */
#define PROBE_WAVEFORM_JSON_CAP 20480

typedef struct {
    uint32_t channels;
    uint32_t head; /* 下一个写入位置 */
    float buf[PROBE_WAVEFORM_SAMPLES];
    char json[PROBE_WAVEFORM_JSON_CAP]; /* get_param 非实时线程格式化用 */
} ProbeWaveformState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif
