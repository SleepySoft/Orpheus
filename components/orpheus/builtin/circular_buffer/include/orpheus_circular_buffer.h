#ifndef ORPHEUS_CIRCULAR_BUFFER_H
#define ORPHEUS_CIRCULAR_BUFFER_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t channels;
    uint32_t frame_size;
    uint32_t hop_size;
    uint32_t num_frames;
    uint32_t history_len;
    uint32_t max_input_frames;
    float* history;  /* channels * history_len */
    float* scratch;  /* channels * (history_len + max_input_frames) */
} CircularBufferState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif /* ORPHEUS_CIRCULAR_BUFFER_H */
