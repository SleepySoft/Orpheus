#ifndef ORPHEUS_EMBED_OUT_H
#define ORPHEUS_EMBED_OUT_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 嵌入输出占位组件：process 不做任何 IO，只把输入 buffer 拷贝到用户消费区。
   - dst：用户每块处理后就绪的输出区（interleaved f32，channels 通道）；
   - dst_capacity：输出区容量（帧），不足则丢弃本块（不报错）。
   生成工程会自动提供 g_embed_out_<node> 缓冲与 orpheus_embed_out_state_<node>()
   访问器，用户在 platform_io.c 的 USER CODE 段消费。 */
typedef struct {
    uint32_t channels;
    uint32_t sample_rate;
    float* dst;            /* 输出区（用户消费） */
    uint32_t dst_capacity; /* 输出区容量（帧） */
} EmbedOutState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif /* ORPHEUS_EMBED_OUT_H */
