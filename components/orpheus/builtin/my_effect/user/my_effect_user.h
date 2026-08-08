#ifndef ORPHEUS_MY_EFFECT_USER_H
#define ORPHEUS_MY_EFFECT_USER_H

#include "orpheus_abi.h"
#include "orpheus_my_effect.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 用户入口点：只改 user/my_effect_user.c。此文件由脚手架创建后生成器不再触碰。 */
int my_effect_user_prepare(MyEffectState* s, const OrpheusConfig* config);
int my_effect_user_reset(MyEffectState* s);
int my_effect_user_process(MyEffectState* s, const OrpheusProcessContext* ctx);
int my_effect_user_handle(MyEffectState* s, uint32_t id, uint32_t event,
                       const OrpheusBlob* req, OrpheusBlob* resp);

#ifdef __cplusplus
}
#endif

#endif /* ORPHEUS_MY_EFFECT_USER_H */
