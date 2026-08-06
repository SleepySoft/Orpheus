#ifndef ORPHEUS_BIQUAD_BANK_H
#define ORPHEUS_BIQUAD_BANK_H

#include "orpheus_abi.h"
#include "orpheus_biquad.h"   /* 复用基础组件：公开状态结构体（deps 声明） */

#ifdef __cplusplus
extern "C" {
#endif

#define BIQUAD_BANK_STAGES 2

/* 聚合组件：内嵌 BIQUAD_BANK_STAGES 个子块（物理连续），
   父组件代理注册子块的参数字段与系数 buffer（BULK 直写）。 */
typedef struct {
    BiquadState bq[BIQUAD_BANK_STAGES];
    uint32_t channels;
} BiquadBankState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif /* ORPHEUS_BIQUAD_BANK_H */
