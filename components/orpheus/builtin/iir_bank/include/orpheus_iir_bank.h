#ifndef ORPHEUS_IIR_BANK_H
#define ORPHEUS_IIR_BANK_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define IIR_BANK_MAX_STAGES    16
#define IIR_BANK_MAX_CHANNELS  32

/* 每通道每级保存状态（z1/z2） */
typedef struct {
    float z1[IIR_BANK_MAX_STAGES];
    float z2[IIR_BANK_MAX_STAGES];
} IirChannelState;

typedef struct {
    /*
     * BULK 直写目标。
     * shared 模式：只使用前 5*MAX_STAGES 个（80 float）。
     * per_channel 模式：按 [channel][stage*5+k] 展开，共 channels*5*MAX_STAGES 个。
     */
    float coefs[IIR_BANK_MAX_CHANNELS][5 * IIR_BANK_MAX_STAGES];
    IirChannelState channelStates[IIR_BANK_MAX_CHANNELS];
    uint32_t numStages;
    uint32_t channels;
    uint32_t coefs_mode;  /* 0=shared, 1=per_channel */
} IirBankState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif /* ORPHEUS_IIR_BANK_H */
