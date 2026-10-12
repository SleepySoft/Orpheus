#ifndef ORPHEUS_INPUT_SELECT_H
#define ORPHEUS_INPUT_SELECT_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_CH 32

typedef struct {
    int32_t map[MAX_CH];   /* map[out] = 0-based input index, or -1 = mute */
    int32_t base_map[MAX_CH];
    uint32_t channels_in;
    uint32_t channels_out;
    char select[256];      /* 参数缓存（restart_required） */
    char override_select[256]; /* 1-based override；0=沿用 base */
} InputSelectState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif /* ORPHEUS_INPUT_SELECT_H */
