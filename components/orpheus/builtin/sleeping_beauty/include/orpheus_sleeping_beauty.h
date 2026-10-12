#ifndef ORPHEUS_SLEEPING_BEAUTY_H
#define ORPHEUS_SLEEPING_BEAUTY_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SB_MAX_RAMPERS   4    /* left, right, center, mono */
#define SB_MAX_CHANNELS  64
#define SB_MAX_TABLE     30
#define SB_SILENT_GAIN   5.0118723e-7f   /* -126 dB */

typedef struct {
    float currentGain;
    float targetGain;
    float rampCoeff;       /* per-sample multiplicative coefficient */
    uint32_t frameCount;   /* remaining process blocks */
} SBRamper;

typedef struct {
    SBRamper rampers[SB_MAX_RAMPERS];
    float    tableDb[SB_MAX_TABLE];
    float    tableIdx[SB_MAX_TABLE];
    uint32_t tableSize;
    int32_t  chanMap[SB_MAX_CHANNELS];   /* 通道->ramper (0-3)，-1=静音 */
    float    gainIndex;    /* 调音参数：增益位置 0-255 */
    float    offset;       /* 中心位置 */
    float    mutesBass;    /* 极端时是否静音低音 */
    float    rampMs;       /* 斜坡时间 */
    float    rampDbPerSec; /* >0 时优先按 dB/s 推导时长 */
    int32_t  freeze;       /* 非零时冻结 current/frameCount */
    int32_t  centerMonoMean; /* 非零时 center/mono=(left+right)/2 */
    uint32_t channels;
    float    sampleRate;
    uint32_t blockSize;
} SleepingBeautyState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif /* ORPHEUS_SLEEPING_BEAUTY_H */
