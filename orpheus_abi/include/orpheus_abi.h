#ifndef ORPHEUS_ABI_H
#define ORPHEUS_ABI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ABI 版本 */
#define ORPHEUS_ABI_VERSION 1

/* 平台导出宏 */
#ifndef ORPHEUS_API
  #if defined(_WIN32)
    #ifdef ORPHEUS_BUILDING_COMPONENT
      #define ORPHEUS_API __declspec(dllexport)
    #else
      #define ORPHEUS_API __declspec(dllimport)
    #endif
  #else
    #define ORPHEUS_API __attribute__((visibility("default")))
  #endif
#endif

/* 错误码 */
typedef enum {
    ORPHEUS_OK = 0,
    ORPHEUS_ERR_INVALID_ARG = -1,
    ORPHEUS_ERR_UNSUPPORTED = -2,
    ORPHEUS_ERR_OUT_OF_MEMORY = -3,
    ORPHEUS_ERR_ABI_MISMATCH = -4,
    ORPHEUS_ERR_PROCESSING = -5,
    ORPHEUS_ERR_NOT_FOUND = -6
} OrpheusResult;

/* 值类型 */
typedef enum {
    ORPHEUS_VALUE_FLOAT = 0,
    ORPHEUS_VALUE_INT = 1,
    ORPHEUS_VALUE_BOOL = 2,
    ORPHEUS_VALUE_STRING = 3,
    ORPHEUS_VALUE_BULK_REF = 4  /* 引用 Bulk 数据区 */
} OrpheusValueType;

typedef struct {
    OrpheusValueType type;
    union {
        float f32;
        int32_t i32;
        bool b;
        const char* str;
        uint32_t bulk_id;
    } value;
} OrpheusValue;

/* 端口方向 */
typedef enum {
    ORPHEUS_PORT_INPUT = 0,
    ORPHEUS_PORT_OUTPUT = 1
} OrpheusPortDirection;

/* 端口类型 */
typedef enum {
    ORPHEUS_PORT_AUDIO = 0,
    ORPHEUS_PORT_CONTROL = 1,
    ORPHEUS_PORT_BULK = 2,
    ORPHEUS_PORT_DEBUG = 3
} OrpheusPortType;

/* 采样格式 */
typedef enum {
    ORPHEUS_FORMAT_F32 = 0,
    ORPHEUS_FORMAT_I16 = 1,
    ORPHEUS_FORMAT_I24 = 2,
    ORPHEUS_FORMAT_I32 = 3
} OrpheusSampleFormat;

/* 端口描述符 */
typedef struct {
    const char* id;
    OrpheusPortDirection direction;
    OrpheusPortType type;
    OrpheusSampleFormat sample_format;
    uint32_t channels;     /* 0 表示由参数决定（构建时解析） */
    uint32_t sample_rate;  /* 0 表示继承 Task 采样率 */
    uint32_t block_size;   /* 0 表示继承 Task 块长度 */
    bool is_variable;      /* true 表示该端口签名由参数决定 */
    const char* channels_param; /* 若 is_variable，指向参数 id */
} OrpheusPort;

/* 参数更新策略 */
typedef enum {
    ORPHEUS_UPDATE_IMMEDIATE = 0,
    ORPHEUS_UPDATE_BLOCK_BOUNDARY = 1,
    ORPHEUS_UPDATE_SMOOTHED = 2,
    ORPHEUS_UPDATE_TRANSACTIONAL = 3,
    ORPHEUS_UPDATE_RESTART_REQUIRED = 4
} OrpheusUpdatePolicy;

/* 参数描述符 */
typedef struct {
    const char* id;
    const char* name;
    OrpheusValueType type;
    OrpheusValue default_value;
    float min_f32;
    float max_f32;
    int32_t min_i32;
    int32_t max_i32;
    const char* unit;
    OrpheusUpdatePolicy update_policy;
    bool readback;
    bool persistent;
    bool affects_signature; /* 改变此参数是否需要重新编译执行计划 */
} OrpheusParameter;

/* 组件描述符 */
typedef struct {
    const char* id;
    const char* version;
    uint32_t abi_version;
    const OrpheusPort* ports;
    uint32_t port_count;
    const OrpheusParameter* params;
    uint32_t param_count;
    size_t state_size;
    size_t scratch_size;
    size_t alignment;
    uint32_t latency_samples;
    bool realtime_safe;
    bool supports_inplace;
} OrpheusComponentDescriptor;

/* Buffer 描述（运行时传入） */
typedef struct {
    void* data;
    OrpheusSampleFormat format;
    uint32_t channels;
    uint32_t frame_capacity; /* 最大可容纳帧数 */
    uint32_t frame_count;    /* 当前有效帧数 */
    bool interleaved;        /* true: 交错布局；false: 平面布局 */
} OrpheusBuffer;

/* 处理上下文 */
typedef struct {
    void* state;
    const OrpheusBuffer* const* inputs;
    OrpheusBuffer** outputs;
    uint32_t input_count;
    uint32_t output_count;
    uint32_t frame_count;
    uint32_t sample_rate;    /* 当前处理采样率 */
    void* scratch;
    size_t scratch_size;
    double timestamp;        /* 秒，可选 */
} OrpheusProcessContext;

/* 配置上下文（prepare 时传入） */
typedef struct {
    uint32_t sample_rate;
    uint32_t block_size;
    uint32_t channels;       /* 主通道数，可变端口可覆盖 */
    const char** param_ids;       /* 参数 ID 数组，与 param_values 一一对应 */
    const OrpheusValue* param_values;
    uint32_t param_count;
} OrpheusConfig;

/* 组件接口函数表 */
typedef struct {
    /* 元数据 */
    const OrpheusComponentDescriptor* (*get_descriptor)(void);

    /* 实例创建/销毁 */
    int (*create)(void** state, const OrpheusConfig* config);
    int (*destroy)(void* state);

    /* 准备/重置 */
    int (*prepare)(void* state, const OrpheusConfig* config);
    int (*reset)(void* state);

    /* 实时处理 */
    int (*process)(void* state, const OrpheusProcessContext* ctx);

    /* 参数访问 */
    int (*set_parameter)(void* state, const char* param_id, const OrpheusValue* value);
    int (*get_parameter)(void* state, const char* param_id, OrpheusValue* value);

    /* 调试：获取内部状态（可选） */
    int (*get_state_value)(void* state, const char* key, OrpheusValue* value);
} OrpheusComponentInterface;

/* 组件唯一导出入口 */
typedef const OrpheusComponentInterface* (*OrpheusGetInterfaceFn)(void);

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif /* ORPHEUS_ABI_H */
