#pragma once
#include "orpheus_abi.h"

/* input_index、drive_port_no、active 由壳填写，算法不得改写。
   count 组仅 [0, <接口名>_count) 有效；零上界数组保留一个无效占位。 */

typedef struct orpheus_builtin_sport_tdm_out_InputF32 {
    const OrpheusBuffer* buffer;
    const float* samples;
    uint32_t input_index;
} orpheus_builtin_sport_tdm_out_InputF32;
typedef struct orpheus_builtin_sport_tdm_out_OutputF32 {
    OrpheusBuffer* buffer;
    float* samples;
    OrpheusDelivery delivery;
    uint32_t drive_port_no;
    bool active;
} orpheus_builtin_sport_tdm_out_OutputF32;

typedef struct orpheus_builtin_sport_tdm_out_InputI32 {
    const OrpheusBuffer* buffer;
    const int32_t* samples;
    uint32_t input_index;
} orpheus_builtin_sport_tdm_out_InputI32;
typedef struct orpheus_builtin_sport_tdm_out_OutputI32 {
    OrpheusBuffer* buffer;
    int32_t* samples;
    OrpheusDelivery delivery;
    uint32_t drive_port_no;
    bool active;
} orpheus_builtin_sport_tdm_out_OutputI32;

typedef struct orpheus_builtin_sport_tdm_out_InputI16 {
    const OrpheusBuffer* buffer;
    const int16_t* samples;
    uint32_t input_index;
} orpheus_builtin_sport_tdm_out_InputI16;
typedef struct orpheus_builtin_sport_tdm_out_OutputI16 {
    OrpheusBuffer* buffer;
    int16_t* samples;
    OrpheusDelivery delivery;
    uint32_t drive_port_no;
    bool active;
} orpheus_builtin_sport_tdm_out_OutputI16;

enum {
    ORPHEUS_BUILTIN_SPORT_TDM_OUT_INPUT_IN = 0,
};
typedef struct orpheus_builtin_sport_tdm_out_IO {
    orpheus_builtin_sport_tdm_out_InputF32 in;
} orpheus_builtin_sport_tdm_out_IO;
