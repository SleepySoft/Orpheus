#ifndef ORPHEUS_EMBED_IN_H
#define ORPHEUS_EMBED_IN_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 嵌入输入占位组件：process 不做任何 IO，只从用户填充区拷贝到输出 buffer。
   - src：用户每块处理前写入的输入区（interleaved f32，channels 通道）；
   - src_frames：用户提供的有效帧数；不足块长时补零并累计 underruns。
   生成工程会自动提供 g_embed_in_<node> 缓冲与 orpheus_embed_in_state_<node>()
   访问器，用户在 platform_io.c 的 USER CODE 段填充。 */
typedef struct {
    uint32_t channels;
    uint32_t sample_rate;
    const float* src;      /* 输入区（用户填充，可为 NULL=静音） */
    uint32_t src_frames;   /* 用户提供的有效帧数 */
    uint32_t underruns;    /* 欠载计数（PROBE） */
} EmbedInState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif /* ORPHEUS_EMBED_IN_H */
