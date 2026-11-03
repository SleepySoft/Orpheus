#ifndef ORPHEUS_CHANNEL_SELECTOR_H
#define ORPHEUS_CHANNEL_SELECTOR_H
#include "orpheus_abi.h"
#ifdef __cplusplus
extern "C" {
#endif

#define CHANNEL_SELECTOR_MAX_CHANNELS 64
typedef struct {
    uint32_t channels_a, channels_b, channels_out;
    int32_t index_base, high_index_policy;
    bool apply_gain, owns_state;
    int32_t indices[CHANNEL_SELECTOR_MAX_CHANNELS];
    float gains[CHANNEL_SELECTOR_MAX_CHANNELS];
} ChannelSelectorState;
ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif
#endif
