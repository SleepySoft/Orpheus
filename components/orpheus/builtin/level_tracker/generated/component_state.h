#pragma once
#include "component_api.h"
#include "../user/algorithm.h"

typedef struct orpheus_builtin_level_tracker_Parameters {
    float rms_db;
    float peak_db;
    float prediction_db_spl;
    float slow_decay_db;
    float hold_margin_db;
    float hold_calls;
    float fast_decay_db;
    float min_db;
    float max_db;
    float volume_db;
    float trigger;
    float trigger_threshold;
    float fast_multiplier;
    float mode;
    float volume_epsilon;
    float level_db_spl;
    float envelope_db;
    bool holding;
} orpheus_builtin_level_tracker_Parameters;

/* validate 为非实时纯校验：候选参数有效后、任何既有 State 改动前调用。
   不得改写外部状态或保留候选指针；错误原样返回，不调用可变生命周期入口。 */

typedef struct orpheus_builtin_level_tracker_State {
    float rms_db;
    float peak_db;
    float prediction_db_spl;
    float slow_decay_db;
    float hold_margin_db;
    float hold_calls;
    float fast_decay_db;
    float min_db;
    float max_db;
    float volume_db;
    float trigger;
    float trigger_threshold;
    float fast_multiplier;
    float mode;
    float volume_epsilon;
    float level_db_spl;
    float envelope_db;
    bool holding;
#if defined(__cplusplus)
    alignas(8) bool _orpheus_alive;
#else
    _Alignas(8) bool _orpheus_alive;
#endif
    bool _orpheus_cleanup_pending;
    orpheus_builtin_level_tracker_AlgorithmState algorithm;
} orpheus_builtin_level_tracker_State;

#if defined(__cplusplus)
extern "C" {
#endif
int orpheus_builtin_level_tracker_algorithm(orpheus_builtin_level_tracker_State* state, orpheus_builtin_level_tracker_IO* io, const OrpheusProcessContext* ctx);
/* create 不调用用户 prepare；prepare 应用配置后调用用户入口，失败自动 destroy 收尾。
   set 先校验，再以旧 State 调用户 hook，成功提交当前字段，失败恢复生成参数。
   get 先调用户 hook，仅 NOT_FOUND 回退字段读取；用户必须填写返回值的 type。
   reset 入口可更新运行状态；未声明时清空 algorithm（有 destroy 时先收尾）。 */
int orpheus_builtin_level_tracker_validate(const orpheus_builtin_level_tracker_Parameters* parameters);
int orpheus_builtin_level_tracker_prepare(orpheus_builtin_level_tracker_State* state, const OrpheusConfig* config);
int orpheus_builtin_level_tracker_reset(orpheus_builtin_level_tracker_State* state);
#if defined(__cplusplus)
}
#endif
