#ifndef ORPHEUS_INPUT_MIXER_3D_H
#define ORPHEUS_INPUT_MIXER_3D_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define IM3D_MAX_CHANNELS 32

typedef struct {
    /* BULK 直写目标：output_channels × input_channels 权重矩阵，行优先 */
    float weights[IM3D_MAX_CHANNELS * IM3D_MAX_CHANNELS];
    float gain_db;
    float gain_linear;
    uint32_t inputChannels;
    uint32_t outputChannels;
} InputMixer3DState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif /* ORPHEUS_INPUT_MIXER_3D_H */
