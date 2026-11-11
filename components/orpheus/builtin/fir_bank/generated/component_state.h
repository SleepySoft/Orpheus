#pragma once
#include "component_api.h"
#include "../user/algorithm.h"

typedef struct orpheus_builtin_fir_bank_Parameters {
    int32_t channels;
    int32_t num_taps;
    char taps_per_channel[1024];
    char coefs_mode[16];
    int32_t num_filters;
    char filter_indices[1024];
    int32_t filter_indices_i32[64];
    char coefs[65536];
    char initial_coefs_per_channel[65536];
    char initial_coefs_banked[65536];
    int32_t processing_enabled;
} orpheus_builtin_fir_bank_Parameters;

/* normalize 只规范化候选参数；key 为 NULL 表示完整配置，否则为本次设置字段。
   不得访问既有状态或保留候选指针；随后重新检查声明约束，再调用 validate。 */

/* validate 为非实时纯校验：候选参数有效后、任何既有 State 改动前调用。
   不得改写外部状态或保留候选指针；错误原样返回，不调用可变生命周期入口。 */

typedef struct orpheus_builtin_fir_bank_State {
    int32_t channels;
    int32_t num_taps;
    char taps_per_channel[1024];
    char coefs_mode[16];
    int32_t num_filters;
    char filter_indices[1024];
    int32_t filter_indices_i32[64];
    char coefs[65536];
    char initial_coefs_per_channel[65536];
    char initial_coefs_banked[65536];
    int32_t processing_enabled;
#if defined(__cplusplus)
    alignas(8) bool _orpheus_alive;
#else
    _Alignas(8) bool _orpheus_alive;
#endif
    bool _orpheus_cleanup_pending;
    orpheus_builtin_fir_bank_AlgorithmState algorithm;
} orpheus_builtin_fir_bank_State;

#if defined(__cplusplus)
extern "C" {
#endif
int orpheus_builtin_fir_bank_algorithm(orpheus_builtin_fir_bank_State* state, orpheus_builtin_fir_bank_IO* io, const OrpheusProcessContext* ctx);
/* create 不调用用户 prepare；prepare 应用配置后调用用户入口，失败自动 destroy 收尾。
   set 先校验，再以旧 State 调用户 hook，成功提交当前字段，失败恢复生成参数。
   get 先调用户 hook，仅 NOT_FOUND 回退字段读取；用户必须填写返回值的 type。
   reset 入口可更新运行状态；未声明时清空 algorithm（有 destroy 时先收尾）。 */
int orpheus_builtin_fir_bank_normalize(orpheus_builtin_fir_bank_Parameters* parameters, const char* key);
int orpheus_builtin_fir_bank_validate(const orpheus_builtin_fir_bank_Parameters* parameters);
int orpheus_builtin_fir_bank_prepare(orpheus_builtin_fir_bank_State* state, const OrpheusConfig* config);
int orpheus_builtin_fir_bank_reset(orpheus_builtin_fir_bank_State* state);
int orpheus_builtin_fir_bank_destroy(orpheus_builtin_fir_bank_State* state);
int orpheus_builtin_fir_bank_register_slots(orpheus_builtin_fir_bank_State* state, const OrpheusRegistry* reg);
int orpheus_builtin_fir_bank_register_filter_indices_i32(orpheus_builtin_fir_bank_State* state, const OrpheusRegistry* reg);
int orpheus_builtin_fir_bank_register_coefs(orpheus_builtin_fir_bank_State* state, const OrpheusRegistry* reg);
int orpheus_builtin_fir_bank_register_initial_coefs_per_channel(orpheus_builtin_fir_bank_State* state, const OrpheusRegistry* reg);
int orpheus_builtin_fir_bank_register_initial_coefs_banked(orpheus_builtin_fir_bank_State* state, const OrpheusRegistry* reg);
#if defined(__cplusplus)
}
#endif
