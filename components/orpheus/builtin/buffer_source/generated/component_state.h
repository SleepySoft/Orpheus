#pragma once
#include "component_api.h"
#include "../user/algorithm.h"

typedef struct orpheus_builtin_buffer_source_Parameters {
    int32_t channels;
    bool ready;
    int32_t missing_blocks;
    char audio_storage[1];
    uint32_t _orpheus_config_frames;
} orpheus_builtin_buffer_source_Parameters;

/* normalize 只规范化候选参数；key 为 NULL 表示完整配置，否则为本次设置字段。
   不得访问既有状态或保留候选指针；随后重新检查声明约束，再调用 validate。 */

typedef struct orpheus_builtin_buffer_source_State {
    int32_t channels;
    bool ready;
    int32_t missing_blocks;
    char audio_storage[1];
    uint32_t _orpheus_config_frames;
#if defined(__cplusplus)
    alignas(1) bool _orpheus_alive;
#else
    _Alignas(1) bool _orpheus_alive;
#endif
    bool _orpheus_cleanup_pending;
    orpheus_builtin_buffer_source_AlgorithmState algorithm;
} orpheus_builtin_buffer_source_State;

#if defined(__cplusplus)
extern "C" {
#endif
int orpheus_builtin_buffer_source_algorithm(orpheus_builtin_buffer_source_State* state, orpheus_builtin_buffer_source_IO* io, const OrpheusProcessContext* ctx);
/* create 不调用用户 prepare；prepare 应用配置后调用用户入口，失败自动 destroy 收尾。
   set 先校验，再以旧 State 调用户 hook，成功提交当前字段，失败恢复生成参数。
   get 先调用户 hook，仅 NOT_FOUND 回退字段读取；用户必须填写返回值的 type。
   reset 入口可更新运行状态；未声明时清空 algorithm（有 destroy 时先收尾）。 */
int orpheus_builtin_buffer_source_normalize(orpheus_builtin_buffer_source_Parameters* parameters, const char* key);
int orpheus_builtin_buffer_source_prepare(orpheus_builtin_buffer_source_State* state, const OrpheusConfig* config);
int orpheus_builtin_buffer_source_reset(orpheus_builtin_buffer_source_State* state);
int orpheus_builtin_buffer_source_set_parameter(orpheus_builtin_buffer_source_State* state, const char* key, const OrpheusValue* value);
int orpheus_builtin_buffer_source_register_audio_storage(orpheus_builtin_buffer_source_State* state, const OrpheusRegistry* reg);
#if defined(__cplusplus)
}
#endif
