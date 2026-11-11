#pragma once
#include "component_api.h"
#include "../user/algorithm.h"

typedef struct orpheus_builtin_interp_lut_Parameters {
    char x_axis[8192];
    char y_axis[8192];
    float x;
    float y;
    char history[2048];
    int32_t channels;
} orpheus_builtin_interp_lut_Parameters;

/* validate 为非实时纯校验：候选参数有效后、任何既有 State 改动前调用。
   不得改写外部状态或保留候选指针；错误原样返回，不调用可变生命周期入口。 */

typedef struct orpheus_builtin_interp_lut_State {
    char x_axis[8192];
    char y_axis[8192];
    float x;
    float y;
    char history[2048];
    int32_t channels;
#if defined(__cplusplus)
    alignas(8) bool _orpheus_alive;
#else
    _Alignas(8) bool _orpheus_alive;
#endif
    bool _orpheus_cleanup_pending;
    orpheus_builtin_interp_lut_AlgorithmState algorithm;
} orpheus_builtin_interp_lut_State;

#if defined(__cplusplus)
extern "C" {
#endif
int orpheus_builtin_interp_lut_algorithm(orpheus_builtin_interp_lut_State* state, orpheus_builtin_interp_lut_IO* io, const OrpheusProcessContext* ctx);
/* create 不调用用户 prepare；prepare 应用配置后调用用户入口，失败自动 destroy 收尾。
   set 先校验，再以旧 State 调用户 hook，成功提交当前字段，失败恢复生成参数。
   get 先调用户 hook，仅 NOT_FOUND 回退字段读取；用户必须填写返回值的 type。
   reset 入口可更新运行状态；未声明时清空 algorithm（有 destroy 时先收尾）。 */
int orpheus_builtin_interp_lut_validate(const orpheus_builtin_interp_lut_Parameters* parameters);
int orpheus_builtin_interp_lut_prepare(orpheus_builtin_interp_lut_State* state, const OrpheusConfig* config);
int orpheus_builtin_interp_lut_reset(orpheus_builtin_interp_lut_State* state);
#if defined(__cplusplus)
}
#endif
