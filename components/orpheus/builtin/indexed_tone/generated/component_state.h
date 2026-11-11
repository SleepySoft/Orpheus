#pragma once
#include "component_api.h"
#include "../user/algorithm.h"

typedef struct orpheus_builtin_indexed_tone_Parameters {
    int32_t channels;
    float gain_index;
    float offset;
    float scale;
    float max_boost_db;
    float max_cut_db;
    float ramp_ms;
    float ramp_db_per_s;
    bool freeze;
    int32_t filter_order;
    char boost_coefs[512];
    char cut_coefs[512];
    int32_t execution_mode;
    int32_t phase;
    float ramp0_current_gain;
    float ramp0_target_gain;
    float ramp0_coefficient;
    int32_t ramp0_remaining_blocks;
    float ramp1_current_gain;
    float ramp1_target_gain;
    float ramp1_coefficient;
    int32_t ramp1_remaining_blocks;
} orpheus_builtin_indexed_tone_Parameters;

/* validate 为非实时纯校验：候选参数有效后、任何既有 State 改动前调用。
   不得改写外部状态或保留候选指针；错误原样返回，不调用可变生命周期入口。 */

typedef struct orpheus_builtin_indexed_tone_State {
    int32_t channels;
    float gain_index;
    float offset;
    float scale;
    float max_boost_db;
    float max_cut_db;
    float ramp_ms;
    float ramp_db_per_s;
    bool freeze;
    int32_t filter_order;
    char boost_coefs[512];
    char cut_coefs[512];
    int32_t execution_mode;
    int32_t phase;
    float ramp0_current_gain;
    float ramp0_target_gain;
    float ramp0_coefficient;
    int32_t ramp0_remaining_blocks;
    float ramp1_current_gain;
    float ramp1_target_gain;
    float ramp1_coefficient;
    int32_t ramp1_remaining_blocks;
#if defined(__cplusplus)
    alignas(8) bool _orpheus_alive;
#else
    _Alignas(8) bool _orpheus_alive;
#endif
    bool _orpheus_cleanup_pending;
    orpheus_builtin_indexed_tone_AlgorithmState algorithm;
} orpheus_builtin_indexed_tone_State;

#if defined(__cplusplus)
extern "C" {
#endif
int orpheus_builtin_indexed_tone_algorithm(orpheus_builtin_indexed_tone_State* state, orpheus_builtin_indexed_tone_IO* io, const OrpheusProcessContext* ctx);
/* create 不调用用户 prepare；prepare 应用配置后调用用户入口，失败自动 destroy 收尾。
   set 先校验，再以旧 State 调用户 hook，成功提交当前字段，失败恢复生成参数。
   get 先调用户 hook，仅 NOT_FOUND 回退字段读取；用户必须填写返回值的 type。
   reset 入口可更新运行状态；未声明时清空 algorithm（有 destroy 时先收尾）。 */
int orpheus_builtin_indexed_tone_validate(const orpheus_builtin_indexed_tone_Parameters* parameters);
int orpheus_builtin_indexed_tone_prepare(orpheus_builtin_indexed_tone_State* state, const OrpheusConfig* config);
int orpheus_builtin_indexed_tone_reset(orpheus_builtin_indexed_tone_State* state);
int orpheus_builtin_indexed_tone_set_parameter(orpheus_builtin_indexed_tone_State* state, const char* key, const OrpheusValue* value);
int orpheus_builtin_indexed_tone_register_boost_coefs(orpheus_builtin_indexed_tone_State* state, const OrpheusRegistry* reg);
int orpheus_builtin_indexed_tone_register_cut_coefs(orpheus_builtin_indexed_tone_State* state, const OrpheusRegistry* reg);
#if defined(__cplusplus)
}
#endif
