#ifndef ORPHEUS_GAIN_RAMPER_ALGORITHM_H
#define ORPHEUS_GAIN_RAMPER_ALGORITHM_H
#include <stdint.h>
#define GR_MAX_RAMPERS 32
#define GR_MAX_CHANNELS 64
#define GR_SILENT_GAIN 5.0118723e-7f
typedef struct {
    float currentGain,targetGain,rampCoeff;
} RamperSlot;
typedef struct {
    RamperSlot rampers[GR_MAX_RAMPERS];
    int32_t chanMap[GR_MAX_CHANNELS];
    float current_gains[GR_MAX_RAMPERS],target_gains[GR_MAX_RAMPERS],coefficients[GR_MAX_RAMPERS];
    int32_t remaining_blocks[GR_MAX_RAMPERS];
    float sampleRate;
    uint32_t blockSize;
} orpheus_builtin_gain_ramper_AlgorithmState;
#endif
