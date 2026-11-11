#pragma once
#include "component_api.h"
#include "../user/algorithm.h"

typedef struct orpheus_builtin_noise_detector_nlms_State {
    int32_t channels;
    int32_t filter_length;
    float step_size;
    float leakage;
    float eps;
    float time_thres;
    float frame_thres_db;
    float residue_db;
    float erle_db;
    int32_t noise_frames;
    float noise_ratio;
    int32_t clicks;
    float residue_pk;
    char detail[1024];
    char _orpheus_readback_detail[1024];
#if defined(__cplusplus)
    alignas(8) bool _orpheus_alive;
#else
    _Alignas(8) bool _orpheus_alive;
#endif
    bool _orpheus_cleanup_pending;
    orpheus_builtin_noise_detector_nlms_AlgorithmState algorithm;
} orpheus_builtin_noise_detector_nlms_State;

#if defined(__cplusplus)
extern "C" {
#endif
int orpheus_builtin_noise_detector_nlms_algorithm(orpheus_builtin_noise_detector_nlms_State* state, orpheus_builtin_noise_detector_nlms_IO* io, const OrpheusProcessContext* ctx);
/* create 不调用用户 prepare；prepare 应用配置后调用用户入口，失败自动 destroy 收尾。
   set 先校验，再以旧 State 调用户 hook，成功提交当前字段，失败恢复生成参数。
   get 先调用户 hook，仅 NOT_FOUND 回退字段读取；用户必须填写返回值的 type。
   reset 入口可更新运行状态；未声明时清空 algorithm（有 destroy 时先收尾）。 */
int orpheus_builtin_noise_detector_nlms_prepare(orpheus_builtin_noise_detector_nlms_State* state, const OrpheusConfig* config);
int orpheus_builtin_noise_detector_nlms_reset(orpheus_builtin_noise_detector_nlms_State* state);
int orpheus_builtin_noise_detector_nlms_destroy(orpheus_builtin_noise_detector_nlms_State* state);
int orpheus_builtin_noise_detector_nlms_get_parameter(orpheus_builtin_noise_detector_nlms_State* state, const char* key, OrpheusValue* value);
#if defined(__cplusplus)
}
#endif
