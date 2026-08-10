#ifndef ORPHEUS_SLC_MATRIX_MUL_H
#define ORPHEUS_SLC_MATRIX_MUL_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SLC_MM_MAX_TABLES 8       /* 最大插值表数量 */
#define SLC_MM_MAX_ELEM   1024    /* 32x32 */
#define SLC_MM_SILENT_GAIN 5.0118723e-7f  /* -126 dB，防 log(0) */

/* N 表插值 + 一阶 IIR 斜坡 + 矩阵乘一体化组件 */
typedef struct {
    /* 配置（restart_required） */
    uint32_t rows;            /* 输出通道数 */
    uint32_t cols;            /* 输入通道数 */
    uint32_t numTables;       /* 插值表数量 N */
    uint32_t interpMethod;    /* 0=线性, 1=对数线性 */
    float    rampCoeff;       /* IIR 系数 c: active = c*active + (1-c)*target */

    /* 运行时控制（immediate） */
    float    interpIndex;     /* 插值位置（如 surround_index） */
    int32_t  freeze;          /* 0=渐变, 1=冻结 */

    /* 表数据 */
    float    tables[SLC_MM_MAX_TABLES * SLC_MM_MAX_ELEM]; /* N x rows x cols，表优先行优先 */
    float    interpX[SLC_MM_MAX_TABLES];                  /* N 个插值断点 */

    /* 内部状态 */
    float    target[SLC_MM_MAX_ELEM];  /* 插值后的目标矩阵 */
    float    active[SLC_MM_MAX_ELEM];  /* IIR 渐变后的当前矩阵 */
    int32_t  targetDirty;              /* target 需要重算 */
} SlcMatrixMulState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif /* ORPHEUS_SLC_MATRIX_MUL_H */