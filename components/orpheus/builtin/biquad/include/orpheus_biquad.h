#ifndef ORPHEUS_BIQUAD_H
#define ORPHEUS_BIQUAD_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_CHANNELS 32

typedef struct {
    float b0, b1, b2, a1, a2;
    /* df2t（直接 II 型转置，滚动）延迟单元 */
    float z1[MAX_CHANNELS];
    float z2[MAX_CHANNELS];
    /* df1（传统直接 I 型）输入/输出历史 */
    float x1[MAX_CHANNELS];
    float x2[MAX_CHANNELS];
    float y1[MAX_CHANNELS];
    float y2[MAX_CHANNELS];
    char form[8];        /* "df2t"(默认) | "df1" */
    char type[16];       /* 参数缓存（restart_required） */
    float fc;            /* 参数缓存 */
    float q;             /* 参数缓存 */
    float gain_db;       /* 参数缓存 */
    uint32_t channels;
} BiquadState;

/* 单样本双二阶：form 选择结构。df1=直接 I 型，df2t=直接 II 型转置（滚动）。
   两者传递函数相同：y = b0*x + b1*x1 + b2*x2 - a1*y1 - a2*y2 */
static inline float biquad_tick(BiquadState* s, uint32_t c, float x) {
    if (s->form[0] == 'd' && s->form[2] == '1') {  /* "df1" */
        float y = s->b0 * x + s->b1 * s->x1[c] + s->b2 * s->x2[c]
                - s->a1 * s->y1[c] - s->a2 * s->y2[c];
        s->x2[c] = s->x1[c]; s->x1[c] = x;
        s->y2[c] = s->y1[c]; s->y1[c] = y;
        return y;
    }
    /* "df2t"（默认）：滚动延迟单元，寄存器最少、数值性质好 */
    float y = s->b0 * x + s->z1[c];
    s->z1[c] = s->b1 * x - s->a1 * y + s->z2[c];
    s->z2[c] = s->b2 * x - s->a2 * y;
    return y;
}

static inline void biquad_clear_history(BiquadState* s) {
    for (uint32_t c = 0; c < MAX_CHANNELS; ++c) {
        s->z1[c] = s->z2[c] = 0.0f;
        s->x1[c] = s->x2[c] = s->y1[c] = s->y2[c] = 0.0f;
    }
}

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif
