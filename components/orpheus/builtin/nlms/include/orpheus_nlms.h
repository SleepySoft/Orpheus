#ifndef ORPHEUS_NLMS_H
#define ORPHEUS_NLMS_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t channels;
    uint32_t filter_length;
    float mu;
    float leak;
    float eps;
    float* w;      /* 系数缓冲区，长度 channels * filter_length */
    float* x;      /* 参考信号延迟线，长度 channels * filter_length */
    uint32_t* pos; /* 每个通道的写入位置 */
} NlmsState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif /* ORPHEUS_NLMS_H */
