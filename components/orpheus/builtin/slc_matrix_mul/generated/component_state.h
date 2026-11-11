#pragma once
#include "component_api.h"
#include "../user/algorithm.h"

typedef struct orpheus_builtin_slc_matrix_mul_Parameters {
    int32_t rows;
    int32_t cols;
    int32_t num_tables;
    char tables[65536];
    char interp_x[1024];
    float interp_index;
    int32_t interp_method;
    float ramp_coeff;
    int32_t freeze;
    int32_t execution_mode;
    char requested_gains[32768];
    float active_gains[1024];
    float accepted_targets[1024];
    int32_t sequence;
    int32_t last_sequence;
    bool initialized;
} orpheus_builtin_slc_matrix_mul_Parameters;

/* validate 为非实时纯校验：候选参数有效后、任何既有 State 改动前调用。
   不得改写外部状态或保留候选指针；错误原样返回，不调用可变生命周期入口。 */

typedef struct orpheus_builtin_slc_matrix_mul_State {
    int32_t rows;
    int32_t cols;
    int32_t num_tables;
    char tables[65536];
    char interp_x[1024];
    float interp_index;
    int32_t interp_method;
    float ramp_coeff;
    int32_t freeze;
    int32_t execution_mode;
    char requested_gains[32768];
    float active_gains[1024];
    float accepted_targets[1024];
    int32_t sequence;
    int32_t last_sequence;
    bool initialized;
#if defined(__cplusplus)
    alignas(8) bool _orpheus_alive;
#else
    _Alignas(8) bool _orpheus_alive;
#endif
    bool _orpheus_cleanup_pending;
    orpheus_builtin_slc_matrix_mul_AlgorithmState algorithm;
} orpheus_builtin_slc_matrix_mul_State;

#if defined(__cplusplus)
extern "C" {
#endif
int orpheus_builtin_slc_matrix_mul_algorithm(orpheus_builtin_slc_matrix_mul_State* state, orpheus_builtin_slc_matrix_mul_IO* io, const OrpheusProcessContext* ctx);
/* create 不调用用户 prepare；prepare 应用配置后调用用户入口，失败自动 destroy 收尾。
   set 先校验，再以旧 State 调用户 hook，成功提交当前字段，失败恢复生成参数。
   get 先调用户 hook，仅 NOT_FOUND 回退字段读取；用户必须填写返回值的 type。
   reset 入口可更新运行状态；未声明时清空 algorithm（有 destroy 时先收尾）。 */
int orpheus_builtin_slc_matrix_mul_validate(const orpheus_builtin_slc_matrix_mul_Parameters* parameters);
int orpheus_builtin_slc_matrix_mul_prepare(orpheus_builtin_slc_matrix_mul_State* state, const OrpheusConfig* config);
int orpheus_builtin_slc_matrix_mul_reset(orpheus_builtin_slc_matrix_mul_State* state);
int orpheus_builtin_slc_matrix_mul_register_requested_gains(orpheus_builtin_slc_matrix_mul_State* state, const OrpheusRegistry* reg);
int orpheus_builtin_slc_matrix_mul_register_active_gains(orpheus_builtin_slc_matrix_mul_State* state, const OrpheusRegistry* reg);
int orpheus_builtin_slc_matrix_mul_register_accepted_targets(orpheus_builtin_slc_matrix_mul_State* state, const OrpheusRegistry* reg);
#if defined(__cplusplus)
}
#endif
