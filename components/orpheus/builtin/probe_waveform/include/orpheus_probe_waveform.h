#ifndef ORPHEUS_PROBE_WAVEFORM_H
#define ORPHEUS_PROBE_WAVEFORM_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PROBE_WAVEFORM_SAMPLES 1023 /* Bridge 单帧最多承载 1023 个 32 位采样 */

typedef struct {
    uint32_t channels;
    uint32_t head; /* 下一个写入位置 */
    float buf[PROBE_WAVEFORM_SAMPLES];
} ProbeWaveformState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif
