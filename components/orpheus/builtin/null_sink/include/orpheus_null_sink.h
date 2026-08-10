#ifndef ORPHEUS_NULL_SINK_H
#define ORPHEUS_NULL_SINK_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 空接收器状态（布局即契约，生成路径按类型拼接） */
typedef struct {
    uint32_t channels;
} NullSinkState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif /* ORPHEUS_NULL_SINK_H */
