#ifndef ORPHEUS_CHANNEL_ROUTER_H
#define ORPHEUS_CHANNEL_ROUTER_H

#include "orpheus_abi.h"

#define CR_MAX_CH 32

typedef struct {
    uint32_t channels_in;
    uint32_t channels_out;
    int32_t map[CR_MAX_CH];  /* 输出通道 o -> 输入通道 map[o]，-1 表示静音 */
    char indices_str[2048];
} ChannelRouterState;

#endif /* ORPHEUS_CHANNEL_ROUTER_H */
