#pragma once
#include "component_api.h"
#include "../user/algorithm.h"

typedef struct orpheus_builtin_indexed_gain_Parameters {
    int32_t channels;
    float gain_index;
    float gain_offset_db;
    float max_gain_db;
    float initial_gain;
    bool apply_index_on_prepare;
    float ramp_ms;
    float ramp_db_per_s;
    bool freeze;
    float current_gain;
    char table_idx[2048];
    char table_db[2048];
} orpheus_builtin_indexed_gain_Parameters;

/* validate 为非实时纯校验：候选参数有效后、任何既有 State 改动前调用。
   不得改写外部状态或保留候选指针；错误原样返回，不调用可变生命周期入口。 */

typedef struct orpheus_builtin_indexed_gain_State {
    int32_t channels;
    float gain_index;
    float gain_offset_db;
    float max_gain_db;
    float initial_gain;
    bool apply_index_on_prepare;
    float ramp_ms;
    float ramp_db_per_s;
    bool freeze;
    float current_gain;
    char table_idx[2048];
    char table_db[2048];
#if defined(__cplusplus)
    alignas(8) bool _orpheus_alive;
#else
    _Alignas(8) bool _orpheus_alive;
#endif
    bool _orpheus_cleanup_pending;
    orpheus_builtin_indexed_gain_AlgorithmState algorithm;
} orpheus_builtin_indexed_gain_State;

#if defined(__cplusplus)
extern "C" {
#endif
int orpheus_builtin_indexed_gain_algorithm(orpheus_builtin_indexed_gain_State* state, orpheus_builtin_indexed_gain_IO* io, const OrpheusProcessContext* ctx);
/* create 不调用用户 prepare；prepare 应用配置后调用用户入口，失败自动 destroy 收尾。
   set 先校验，再以旧 State 调用户 hook，成功提交当前字段，失败恢复生成参数。
   get 先调用户 hook，仅 NOT_FOUND 回退字段读取；用户必须填写返回值的 type。
   reset 入口可更新运行状态；未声明时清空 algorithm（有 destroy 时先收尾）。 */
int orpheus_builtin_indexed_gain_validate(const orpheus_builtin_indexed_gain_Parameters* parameters);
int orpheus_builtin_indexed_gain_prepare(orpheus_builtin_indexed_gain_State* state, const OrpheusConfig* config);
int orpheus_builtin_indexed_gain_reset(orpheus_builtin_indexed_gain_State* state);
int orpheus_builtin_indexed_gain_set_parameter(orpheus_builtin_indexed_gain_State* state, const char* key, const OrpheusValue* value);
#if defined(__cplusplus)
}
#endif
