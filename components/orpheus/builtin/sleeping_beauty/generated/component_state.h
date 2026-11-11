#pragma once
#include "component_api.h"
#include "../user/algorithm.h"

typedef struct orpheus_builtin_sleeping_beauty_Parameters {
    float gain_index;
    float offset;
    float mutes_bass;
    float ramp_ms;
    float ramp_db_per_s;
    bool freeze;
    char center_mono_mode[16];
    bool apply_index_on_prepare;
    bool preserve_state_on_reset;
    int32_t channels;
    char chan_map[1024];
    char table_idx[1024];
    char table_db[1024];
} orpheus_builtin_sleeping_beauty_Parameters;

/* validate 为非实时纯校验：候选参数有效后、任何既有 State 改动前调用。
   不得改写外部状态或保留候选指针；错误原样返回，不调用可变生命周期入口。 */

typedef struct orpheus_builtin_sleeping_beauty_State {
    float gain_index;
    float offset;
    float mutes_bass;
    float ramp_ms;
    float ramp_db_per_s;
    bool freeze;
    char center_mono_mode[16];
    bool apply_index_on_prepare;
    bool preserve_state_on_reset;
    int32_t channels;
    char chan_map[1024];
    char table_idx[1024];
    char table_db[1024];
#if defined(__cplusplus)
    alignas(8) bool _orpheus_alive;
#else
    _Alignas(8) bool _orpheus_alive;
#endif
    bool _orpheus_cleanup_pending;
    orpheus_builtin_sleeping_beauty_AlgorithmState algorithm;
} orpheus_builtin_sleeping_beauty_State;

#if defined(__cplusplus)
extern "C" {
#endif
int orpheus_builtin_sleeping_beauty_algorithm(orpheus_builtin_sleeping_beauty_State* state, orpheus_builtin_sleeping_beauty_IO* io, const OrpheusProcessContext* ctx);
/* create 不调用用户 prepare；prepare 应用配置后调用用户入口，失败自动 destroy 收尾。
   set 先校验，再以旧 State 调用户 hook，成功提交当前字段，失败恢复生成参数。
   get 先调用户 hook，仅 NOT_FOUND 回退字段读取；用户必须填写返回值的 type。
   reset 入口可更新运行状态；未声明时清空 algorithm（有 destroy 时先收尾）。 */
int orpheus_builtin_sleeping_beauty_validate(const orpheus_builtin_sleeping_beauty_Parameters* parameters);
int orpheus_builtin_sleeping_beauty_prepare(orpheus_builtin_sleeping_beauty_State* state, const OrpheusConfig* config);
int orpheus_builtin_sleeping_beauty_reset(orpheus_builtin_sleeping_beauty_State* state);
int orpheus_builtin_sleeping_beauty_set_parameter(orpheus_builtin_sleeping_beauty_State* state, const char* key, const OrpheusValue* value);
#if defined(__cplusplus)
}
#endif
