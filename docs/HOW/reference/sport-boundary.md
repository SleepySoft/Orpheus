# SPORT Boundary、Clock Domain 与 Trigger Group

> 状态：ADSP-21593 V1 已实现并通过 CCES 2.11.1 生产壳集成构建。

## 1. 目标

让生成图直接被 SPORT/PDMA callback 调用，同时把三件事分开：

- SPORT Boundary：DMA slot/格式与逻辑通道映射；
- Clock Domain：共享同一绝对样本编号的时间轴；
- Trigger Group：哪些硬件 buffer 共同形成一次 Task 调用。

Orpheus 不配置 PCG/SRU、DMA descriptor 或中断号。用户/Target Profile 负责硬件初始化，并对 `user_guaranteed` 的锁相关系负责。

## 2. 工程契约

```yaml
clock_domains:
  - id: audio48
    sample_rate: 48000
    assurance: user_guaranteed

trigger_groups:
  - id: a2b_block
    clock_domain: audio48
    dispatch: master
    master: sport0a_rx
    members: [sport0a_rx, sport0b_rx]

tasks:
  - id: audio
    sample_rate: 48000
    block_size: 32
    clock_domain: audio48
    trigger_group: a2b_block
```

`assurance`：

- `derived`：同一硬件源的整数分频，可由平台配置证明；
- `user_guaranteed`：板级工程声明锁相，Orpheus 编译允许、运行监测；
- `measured`：平台有外部测量/校准证据，语义仍由用户负责。

V1 dispatch：

- `caller`：调用者保证所有 DMA buffer 已就绪；
- `master`：`master` callback 驱动，`members` 是同块 follower。

V1 不生成多 callback barrier；需要 barrier 时由板级 adapter 收齐相同 sequence 后调用生成 Task API。

## 3. SPORT 边界

图节点使用 `orpheus.builtin.sport_tdm_in/out`，仅支持 `adsp21593` 平台。节点声明逻辑通道、采样率和所属 Task；物理映射位于顶层 `sport_bindings`：

```yaml
sport_bindings:
  - node: sport_in
    streams:
      - resource: sport0b_rx
        slots: [0, 1]
        channels: [0, 1]
        slot_count: 16
        format: q1_31
```

支持格式：`q1_31`、`s24_left_in_s32`、`s24_right_in_s32`、`f32`。生成器静态展开 gather/scatter，无运行期映射表和格式分支。

编译器检查：

- SPORT 节点必须且只能有一个 binding；
- slots/channels 长度一致，slot 不越界；
- 每个逻辑通道恰好映射一次；
- resource 全局唯一；
- master trigger group 覆盖 Task 的全部 SPORT 输入资源；
- Task、Trigger Group 与 Clock Domain 引用和采样率一致。

## 4. 生成 API

每个绑定 SPORT 的外部触发 Task 生成类型化 IO：

```c
typedef struct OrpheusSportIo_audio {
    const int32_t* sport0b_rx;
    const int32_t* sport0a_rx;
    int32_t* sport2a_tx;
    int32_t* sport2b_tx;
} OrpheusSportIo_audio;

typedef struct OrpheusExternalTriggerContext {
    uint64_t epoch;
    uint64_t frame_index;
    uint64_t sequence;
    uint32_t frames;
    uint32_t flags;
} OrpheusExternalTriggerContext;

int orpheus_graph_process_task_audio_sport(
    const OrpheusSportIo_audio* io,
    const OrpheusExternalTriggerContext* trigger);
```

wrapper 顺序：

```text
DMA RX -> slot gather/定点转f32 -> Task_at -> f32转定点/slot scatter -> DMA TX
```

旧 `orpheus_generated_process_task_audio(frame_count)` 保留。外部触发路径新增 `_at` 入口，ProcessContext 使用调用者传入的 epoch/frame/flags。

## 5. 用户保证与运行监测

同 Clock Domain 意味着所有 Task/SPORT 共享同一绝对样本坐标，不等同于“标称采样率相同”。调用者必须提供：

- epoch：重启、重新锁相或不连续后递增；
- frame_index：本块首样本绝对位置；
- sequence：每次 trigger group 调用递增；
- frames：必须等于该 Task 块长；
- flags：首次/失锁恢复时带 DISCONTINUITY。

生成代码统计：

```c
const OrpheusTriggerStats* orpheus_generated_trigger_stats_audio(void);
```

统计 `calls/sequence_errors/frame_errors`。这是契约违约观测，不替代硬件锁相证明。

## 6. 多 SPORT / 多时钟规则

1. 同 clock、同 frame sync、同块相位：一个 master + 多 follower，共用一个 Task。
2. 同域不同块长：每个 Task 独立入口；板级 adapter accumulate/split 后按固定量子调用。
3. 同域独立 callback：调用不同 Task 入口，并传同一 frame 坐标；跨 Task 数据仍走显式同步边界。
4. 相同标称速率但独立晶振：必须声明不同 Clock Domain。async bridge 只能解耦执行抖动，长期漂移需要 ASRC、水位反馈或 sample-slip。
5. 共用硬件时钟的有理数分频：各 Task 保持独立量子，可使用静态 rate bridge/rate_sync。

多 Clock Domain 生成图的全局 `orpheus_generated_process()` 返回 `ORPHEUS_ERR_UNSUPPORTED`，生产宿主必须调用独立 Task 入口。

## 7. ADSP-21593 验证

示例：`examples/adsp21593_sport_gain.yaml`。

EREV 生产壳验证：SPORT0A callback 为 master，同时传入 SPORT0A/0B RX 与 SPORT2A/2B TX；生成 wrapper 完成32通道映射和Q1.31转换。CCES 2.11.1 Release clean build 为0错误0警告。

尚未完成板卡/JTAG音频、cycle deadline、cache coherency和失锁恢复实测。
