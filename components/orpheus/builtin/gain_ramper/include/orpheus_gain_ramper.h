#ifndef ORPHEUS_GAIN_RAMPER_H
#define ORPHEUS_GAIN_RAMPER_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GR_MAX_RAMPERS  8
#define GR_MAX_CHANNELS 32
#define GR_SILENT_GAIN  5.0118723e-7f   /* -126 dB，与 Rgainx rgain_SILENT_GAIN 一致 */

/* 单个 ramper 槽位：指数斜坡状态机 */
typedef struct {
    float currentGain;   /* 当前线性增益（斜坡中） */
    float targetGain;    /* 目标线性增益 */
    float rampCoeff;     /* 每块指数因子（ln(target/current)/numBlocks） */
} RamperSlot;

typedef struct {
    RamperSlot rampers[GR_MAX_RAMPERS];
    int32_t    chanMap[GR_MAX_CHANNELS];  /* 通道->ramper 索引，-1=bypass */
    float      gain_db;       /* 调音参数：目标增益（dB） */
    float      ramp_ms;       /* 调音参数：斜坡时间 */
    uint32_t   channels;
    uint32_t   numRampers;
    float      sampleRate;
    uint32_t   blockSize;
} GainRamperState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif /* ORPHEUS_GAIN_RAMPER_H */
