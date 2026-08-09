#ifndef ORPHEUS_INTERP_LUT_H
#define ORPHEUS_INTERP_LUT_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define IL_MAX_POINTS 256
#define IL_HISTORY 64

typedef struct {
    float axis[IL_MAX_POINTS];
    float table[IL_MAX_POINTS];
    uint32_t count;
    float x;
    float y;
    float hist[IL_HISTORY];
    uint32_t hist_pos;
    uint32_t hist_count;
    uint32_t channels;
    char json_history[2048];
} InterpLutState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif /* ORPHEUS_INTERP_LUT_H */
