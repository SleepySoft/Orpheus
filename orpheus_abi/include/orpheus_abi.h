#ifndef ORPHEUS_ABI_H
#define ORPHEUS_ABI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ABI 版本
   v2: 资源槽注册（register_slots）+ 实例内存块下发（OrpheusConfig.state_block）。
   组件接口表尾部追加字段，旧 DLL（abi_version=1）由 Runtime 按版本号规避访问。 */
#define ORPHEUS_ABI_VERSION 2

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

/* 资源槽类别（v2）：组件向 Runtime 注册的可寻址数据项 */
typedef enum {
    ORPHEUS_SLOT_SETTING = 0,  /* 调音参数，可读写 */
    ORPHEUS_SLOT_COMMAND = 1,  /* 一次性命令，只写 + 确认 */
    ORPHEUS_SLOT_BULK    = 2,  /* 大块数据（双 bank） */
    ORPHEUS_SLOT_PROBE   = 3,  /* 探针：组件写 / Runtime 读 */
    ORPHEUS_SLOT_STATE   = 4   /* 内部状态，调试只读 */
} OrpheusSlotKind;

/* 槽标志位 */
#define ORPHEUS_SLOT_PERSISTENT        (1u << 0)
#define ORPHEUS_SLOT_READBACK          (1u << 1)
#define ORPHEUS_SLOT_AFFECTS_SIGNATURE (1u << 2)
#define ORPHEUS_SLOT_DIRECT_WRITE      (1u << 3)  /* 存储值即语义值，允许 Runtime 直写（无副作用重算） */

/* 槽 ID：64 位，version|core|kind|type|instance|slot（见 docs/design_registry.md） */
typedef uint64_t OrpheusSlotId;
#define ORPHEUS_SLOT_ID_INVALID ((OrpheusSlotId)UINT64_MAX)

/* 槽描述：组件在 register_slots 中逐项提供 */
typedef struct {
    OrpheusSlotKind kind;
    const char* key;             /* 稳定逻辑键，与 manifest 参数/探针 id 对齐 */
    const char* name;            /* 显示名（中文优先） */
    OrpheusValueType type;
    size_t offset;               /* 相对实例状态块基址的偏移 */
    size_t size;                 /* 元素字节数 */
    uint32_t count;              /* 数组长度，1 = 标量 */
    float min_f32;
    float max_f32;
    int32_t min_i32;
    int32_t max_i32;
    const char* unit;
    OrpheusUpdatePolicy update_policy;
    uint32_t flags;
} OrpheusSlotInfo;

/* 注册器：Runtime 提供给组件的注册接口 */
typedef struct OrpheusRegistry {
    void* ctx;  /* 注册上下文（Runtime 传入，指向实例槽表） */
    OrpheusSlotId (*add)(void* ctx, const OrpheusSlotInfo* info);
    int (*update)(void* ctx, OrpheusSlotId id, const OrpheusSlotInfo* info);
} OrpheusRegistry;

/* 一行注册宏：偏移由宏从基址自动计算 */
#define ORPHEUS_REG_SLOT(reg, base, member, kind_, key_, name_, type_, ...) \
    (reg)->add((reg)->ctx, &(OrpheusSlotInfo){ .kind=(kind_), .key=(key_), .name=(name_), \
        .type=(type_), \
        .offset=(size_t)((char*)&((base)->member) - (char*)(base)), \
        .size=sizeof((base)->member), .count=1, __VA_ARGS__ })

/* 数组槽注册宏：count 为元素个数，size 取单个元素大小 */
#define ORPHEUS_REG_ARRAY(reg, base, member, n, kind_, key_, name_, type_, ...) \
    (reg)->add((reg)->ctx, &(OrpheusSlotInfo){ .kind=(kind_), .key=(key_), .name=(name_), \
        .type=(type_), \
        .offset=(size_t)((char*)&((base)->member) - (char*)(base)), \
        .size=sizeof(((base)->member)[0]), .count=(n), __VA_ARGS__ })

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
    void* state_block;       /* v2：统一内存拼接下本实例的连续内存块基址（组件自行布局） */
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

    /* v2：主动注册资源槽（地址/类型/说明）。实现后 Runtime 直接读写槽内存，
       set/get_parameter 退化为兜底。旧 DLL 该字段为 NULL（且 abi_version=1）。 */
    int (*register_slots)(void* state, const OrpheusRegistry* reg);
} OrpheusComponentInterface;

/* 组件唯一导出入口 */
typedef const OrpheusComponentInterface* (*OrpheusGetInterfaceFn)(void);

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif /* ORPHEUS_ABI_H */
