#pragma once
#include "component_api.h"
#include "../user/algorithm.h"

typedef struct orpheus_builtin_channel_selector_Parameters {
    int32_t channels_a;
    int32_t channels_b;
    int32_t channels_out;
    int32_t index_base;
    int32_t high_index_policy;
    bool apply_gain;
    char indices[1024];
    char gains[4096];
    bool accumulate;
    bool enabled;
} orpheus_builtin_channel_selector_Parameters;

/* validate 为非实时纯校验：候选参数有效后、任何既有 State 改动前调用。
   不得改写外部状态或保留候选指针；错误原样返回，不调用可变生命周期入口。 */

typedef struct orpheus_builtin_channel_selector_State {
    int32_t channels_a;
    int32_t channels_b;
    int32_t channels_out;
    int32_t index_base;
    int32_t high_index_policy;
    bool apply_gain;
    char indices[1024];
    char gains[4096];
    bool accumulate;
    bool enabled;
#if defined(__cplusplus)
    alignas(8) bool _orpheus_alive;
#else
    _Alignas(8) bool _orpheus_alive;
#endif
    bool _orpheus_cleanup_pending;
    orpheus_builtin_channel_selector_AlgorithmState algorithm;
} orpheus_builtin_channel_selector_State;

#if defined(__cplusplus)
extern "C" {
#endif
int orpheus_builtin_channel_selector_algorithm(orpheus_builtin_channel_selector_State* state, orpheus_builtin_channel_selector_IO* io, const OrpheusProcessContext* ctx);
/* create 不调用用户 prepare；prepare 应用配置后调用用户入口，失败自动 destroy 收尾。
   set 先校验，再以旧 State 调用户 hook，成功提交当前字段，失败恢复生成参数。
   get 先调用户 hook，仅 NOT_FOUND 回退字段读取；用户必须填写返回值的 type。
   reset 入口可更新运行状态；未声明时清空 algorithm（有 destroy 时先收尾）。 */
int orpheus_builtin_channel_selector_validate(const orpheus_builtin_channel_selector_Parameters* parameters);
int orpheus_builtin_channel_selector_prepare(orpheus_builtin_channel_selector_State* state, const OrpheusConfig* config);
int orpheus_builtin_channel_selector_reset(orpheus_builtin_channel_selector_State* state);
int orpheus_builtin_channel_selector_register_indices(orpheus_builtin_channel_selector_State* state, const OrpheusRegistry* reg);
int orpheus_builtin_channel_selector_register_gains(orpheus_builtin_channel_selector_State* state, const OrpheusRegistry* reg);
#if defined(__cplusplus)
}
#endif
