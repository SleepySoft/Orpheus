#ifndef ORPHEUS_DELAY_LINE_H
#define ORPHEUS_DELAY_LINE_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DELAY_LINE_MAX_CHANNELS 32
#define DELAY_LINE_MAX_DELAY    192000

typedef struct {
    float* buffer;
    uint32_t channels;
    uint32_t max_delay;
    uint32_t write_pos;
    uint32_t capacity;
    float delays_samples[DELAY_LINE_MAX_CHANNELS];
} DelayLineState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif /* ORPHEUS_DELAY_LINE_H */
