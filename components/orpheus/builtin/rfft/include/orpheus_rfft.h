#ifndef ORPHEUS_RFFT_H
#define ORPHEUS_RFFT_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RFFT_MAX_SIZE 1024

typedef struct {
    float twiddleCos[RFFT_MAX_SIZE / 2];  /* 预计算 cos 表 */
    float twiddleSin[RFFT_MAX_SIZE / 2];  /* 预计算 sin 表 */
    float scratchR[RFFT_MAX_SIZE];        /* 实部暂存 */
    float scratchI[RFFT_MAX_SIZE];        /* 虚部暂存 */
    uint32_t fftSize;                     /* FFT 点数（2 的幂） */
    uint32_t channels;
    uint32_t frameSize;                   /* 0 表示与 block_size 相同 */
    uint32_t outputMode;                  /* 0=halfcomplex, 1=magnitude, 2=power */
} RfftState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif /* ORPHEUS_RFFT_H */
