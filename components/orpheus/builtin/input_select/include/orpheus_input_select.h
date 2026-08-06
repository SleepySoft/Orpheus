#ifndef ORPHEUS_INPUT_SELECT_H
#define ORPHEUS_INPUT_SELECT_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_CH 32

typedef struct {
    int32_t map[MAX_CH];   /* map[out] = 0-based input index, or -1 = mute */
    uint32_t channels_in;
    uint32_t channels_out;
    char select[256];      /* 参数缓存（restart_required） */
} InputSelectState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif /* ORPHEUS_INPUT_SELECT_H */
