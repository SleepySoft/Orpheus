#ifndef ORPHEUS_MY_EFFECT_H
#define ORPHEUS_MY_EFFECT_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* MyEffectState：公开状态结构体（骨架字段；算法参数可在此扩展，user 侧读写）。 */
typedef struct {
    uint32_t channels;
    float mix;   /* 混合比（示例参数） */
} MyEffectState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif /* ORPHEUS_MY_EFFECT_H */
