#pragma once
#include "component_api.h"
#include "../user/algorithm.h"

typedef struct orpheus_builtin_balance_State {
    float balance;
    int32_t channels;
    float ramp_ms;
#if defined(__cplusplus)
    alignas(8) bool _orpheus_alive;
#else
    _Alignas(8) bool _orpheus_alive;
#endif
    bool _orpheus_cleanup_pending;
    orpheus_builtin_balance_AlgorithmState algorithm;
} orpheus_builtin_balance_State;

int orpheus_builtin_balance_algorithm(orpheus_builtin_balance_State* state, orpheus_builtin_balance_IO* io, const OrpheusProcessContext* ctx);
/* create 不调用用户 prepare；prepare 应用配置后调用用户入口，失败自动 destroy 收尾。
   set 先校验，再以旧 State 调用户 hook，成功提交当前字段，失败恢复生成参数。
   get 先调用户 hook，仅 NOT_FOUND 回退字段读取；用户必须填写返回值的 type。
   reset 入口可更新运行状态；未声明时清空 algorithm（有 destroy 时先收尾）。 */
int orpheus_builtin_balance_prepare(orpheus_builtin_balance_State* state, const OrpheusConfig* config);
int orpheus_builtin_balance_reset(orpheus_builtin_balance_State* state);
int orpheus_builtin_balance_set_parameter(orpheus_builtin_balance_State* state, const char* key, const OrpheusValue* value);
