#include "my_effect_user.h"

#include <string.h>

/* ===== 用户代码（生成器永不覆盖此文件）=====
 * - prepare/reset/process：DSP 算法实现；
 * - handle：CUSTOM 消息（req->data/len → resp->data/len；resp==NULL 表示 notification）。
 *   返回 ORPHEUS_HOOK_HANDLED = 已处理；ORPHEUS_HOOK_CONTINUE = 交给默认语义。
 */

int my_effect_user_prepare(MyEffectState* s, const OrpheusConfig* config) {
    (void)s; (void)config;
    return ORPHEUS_OK;
}

int my_effect_user_reset(MyEffectState* s) {
    (void)s;
    return ORPHEUS_OK;
}

int my_effect_user_process(MyEffectState* s, const OrpheusProcessContext* ctx) {
    /* 默认直通：输入 → 输出 */
    if (!ctx->inputs[0] || !ctx->outputs[0]) return ORPHEUS_ERR_INVALID_ARG;
    uint32_t n = ctx->frame_count * s->channels;
    memcpy(ctx->outputs[0]->data, ctx->inputs[0]->data, n * sizeof(float));
    ctx->outputs[0]->frame_count = ctx->frame_count;
    return ORPHEUS_OK;
}

int my_effect_user_handle(MyEffectState* s, uint32_t id, uint32_t event,
                       const OrpheusBlob* req, OrpheusBlob* resp) {
    (void)s; (void)id; (void)event;
    /* ???CUSTOM ???????resp==NULL ? notification ?????? */
    if (resp == NULL) return ORPHEUS_HOOK_HANDLED;
    if (req != NULL && req->len > 0) {
        memcpy((void*)resp->data, req->data, req->len);
        resp->len = req->len;
    } else {
        resp->len = 0;
    }
    return ORPHEUS_HOOK_HANDLED;
}
