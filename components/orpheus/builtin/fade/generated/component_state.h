#pragma once
#include "component_api.h"
#include "../user/algorithm.h"

typedef struct orpheus_builtin_fade_Parameters {
    float fade;
    float crossover_hz;
    int32_t channels;
    int32_t front_channels;
    float ramp_ms;
    int32_t execution_mode;
    char chan_map[256];
    char current_gains[4096];
    char target_gains[4096];
    char coefficients[4096];
    char remaining_blocks[256];
    bool freeze;
} orpheus_builtin_fade_Parameters;

/* validate 为非实时纯校验：候选参数有效后、任何既有 State 改动前调用。
   不得改写外部状态或保留候选指针；错误原样返回，不调用可变生命周期入口。 */

typedef struct orpheus_builtin_fade_State {
    float fade;
    float crossover_hz;
    int32_t channels;
    int32_t front_channels;
    float ramp_ms;
    int32_t execution_mode;
    char chan_map[256];
    char current_gains[4096];
    char target_gains[4096];
    char coefficients[4096];
    char remaining_blocks[256];
    bool freeze;
#if defined(__cplusplus)
    alignas(8) bool _orpheus_alive;
#else
    _Alignas(8) bool _orpheus_alive;
#endif
    bool _orpheus_cleanup_pending;
    orpheus_builtin_fade_AlgorithmState algorithm;
} orpheus_builtin_fade_State;

#if defined(__cplusplus)
extern "C" {
#endif
int orpheus_builtin_fade_algorithm(orpheus_builtin_fade_State* state, orpheus_builtin_fade_IO* io, const OrpheusProcessContext* ctx);
/* create 不调用用户 prepare；prepare 应用配置后调用用户入口，失败自动 destroy 收尾。
   set 先校验，再以旧 State 调用户 hook，成功提交当前字段，失败恢复生成参数。
   get 先调用户 hook，仅 NOT_FOUND 回退字段读取；用户必须填写返回值的 type。
   reset 入口可更新运行状态；未声明时清空 algorithm（有 destroy 时先收尾）。 */
int orpheus_builtin_fade_validate(const orpheus_builtin_fade_Parameters* parameters);
int orpheus_builtin_fade_prepare(orpheus_builtin_fade_State* state, const OrpheusConfig* config);
int orpheus_builtin_fade_reset(orpheus_builtin_fade_State* state);
int orpheus_builtin_fade_set_parameter(orpheus_builtin_fade_State* state, const char* key, const OrpheusValue* value);
int orpheus_builtin_fade_register_current_gains(orpheus_builtin_fade_State* state, const OrpheusRegistry* reg);
int orpheus_builtin_fade_register_target_gains(orpheus_builtin_fade_State* state, const OrpheusRegistry* reg);
int orpheus_builtin_fade_register_coefficients(orpheus_builtin_fade_State* state, const OrpheusRegistry* reg);
int orpheus_builtin_fade_register_remaining_blocks(orpheus_builtin_fade_State* state, const OrpheusRegistry* reg);
#if defined(__cplusplus)
}
#endif
