/* host_win.c —— Windows 实时宿主模板（miniaudio 设备时钟）。
 *
 * 本文件是仓库内维护的真实 C 源（orpheus_core/orpheus_core/templates/host_win.c），
 * 生成器在图含设备组件（device_in/device_out，平台解析为 win）时原样复制到生成工程
 * src/host_win.c 作为程序入口。图相关参数全部由 orpheus_host_config.h 宏注入；
 * 图本体（组件实例/连线/参数表）在 main.c，经 orpheus_generated.h 接口调用。
 *
 * 与动态路径 rt_host 同一职责划分：组件 device_in/device_out 只是占位（process 空操作），
 * 宿主在 process 前填充 device_in 输出 buffer、process 后取走 device_out 输入 buffer。
 *
 * 控制协议与 rt_host 一致（stdin 文本，一行一条）：
 *   SET <node> <param> <value>      -> OK/ERR SET（标量槽直写，组件 process 自行平滑）
 *   GET <node> <param>              -> VALUE <node> <param> <value>
 *   BULK <node> <key> <n> <v0>...   -> OK/ERR BULK（BULK 槽直写，块边界提交双 bank）
 *   RESOLVE <id> / MAP              -> RESOLVED 行（内存透明查询）
 *   RW <id> <value> / RR <id>       -> OK/ERR RW、RVALUE（按 ID 标量读写）
 *   RWB <id> <n> <v0>...            -> OK/ERR RWB（按 ID 写 BULK）
 *   GETBULK <node> <key> / RGB <id> -> BULKVALUE 0x<id> <v0>...（读 active bank）
 *   MSG <hex>                       -> MSGRSP <hex> / MSGNONE（二进制消息分发）
 *   STOP（或空行 / EOF）            -> 退出
 * stdout：LOG 生命周期行、PROBE/PROBE_JSON 探针行（每 200ms）、命令回显。
 *
 * 设备拓扑（与 rt_host 一致）：
 *   采集+播放均为默认设备         -> 单 duplex 设备（同一时钟域，最低延迟）
 *   采集+播放但指定设备/loopback  -> 异步桥（capture -> 环形缓冲 -> playback 主时钟），
 *                                    解耦异源时钟并通过水位上报欠载/溢出
 *   仅播放 / 仅采集               -> 单设备即图时钟
 */

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include "orpheus_generated.h"
#include "orpheus_host_config.h"
#include "orpheus_control.h"
#include "orpheus_id_map.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#define HOST_IN_CH  ((uint32_t)ORPHEUS_HOST_IN_CHANNELS)
#define HOST_OUT_CH ((uint32_t)ORPHEUS_HOST_OUT_CHANNELS)
#define HOST_SR     ((uint32_t)ORPHEUS_HOST_SAMPLE_RATE)
#define HOST_BS     ((uint32_t)ORPHEUS_HOST_BLOCK_SIZE)

typedef struct {
    OrpheusBuffer* in_buf;    /* device_in 输出 buffer（process 前填充），可为 NULL */
    OrpheusBuffer* out_buf;   /* device_out 输入 buffer（process 后取走），可为 NULL */
    ma_pcm_rb* rb;            /* 异步桥环形缓冲（duplex/单设备时为 NULL） */
    uint32_t rb_capacity;     /* 环形缓冲容量（帧），无桥为 0 */
    /* 跨线程计数/标志：回调线程写、探针线程读。32 位对齐 volatile 读写在 Windows
     * 上原子，且规避 MSVC 不支持 C11 stdatomic 的工具链问题（与 rt_host 的
     * std::atomic 语义等价——只需宽松可见性，无需顺序约束）。 */
    volatile uint32_t underruns;  /* 播放欠载（rb 空） */
    volatile uint32_t overruns;   /* 采集溢出（rb 满，时钟漂移） */
    volatile int primed;          /* 异步桥：采集已预充水位 */
    uint32_t prime_target;        /* 预充目标（帧） */
} HostContext;

/* 图块长可能很小（如 128 帧 = 2.7ms），对共享模式设备过于激进；
 * 设备周期与图块长解耦（回调内按块长分片），请求一个合理下限（10ms）。 */
static ma_uint32 host_period_frames(void) {
    ma_uint32 floor_frames = HOST_SR / 100;
    return HOST_BS > floor_frames ? HOST_BS : floor_frames;
}

/* ------------------------------------------------------------ 设备枚举/匹配 */

/* 按名称子串（大小写不敏感）查设备 id；match 为空或未命中返回 0。 */
static int host_find_device_id(ma_device_type type, const char* match, ma_device_id* out_id) {
    if (!match || !match[0]) return 0;
    ma_context ctx;
    if (ma_context_init(NULL, 0, NULL, &ctx) != MA_SUCCESS) return 0;
    ma_device_info* p_play = NULL;
    ma_device_info* p_cap = NULL;
    ma_uint32 n_play = 0, n_cap = 0;
    int found = 0;
    if (ma_context_get_devices(&ctx, &p_play, &n_play, &p_cap, &n_cap) == MA_SUCCESS) {
        ma_device_info* infos = (type == ma_device_type_playback) ? p_play : p_cap;
        ma_uint32 count = (type == ma_device_type_playback) ? n_play : n_cap;
        char needle[256];
        size_t i;
        for (i = 0; i < sizeof(needle) - 1 && match[i]; ++i)
            needle[i] = (char)tolower((unsigned char)match[i]);
        needle[i] = (char)0;
        for (ma_uint32 k = 0; k < count && !found; ++k) {
            char name[256];
            for (i = 0; i < sizeof(name) - 1 && infos[k].name[i]; ++i)
                name[i] = (char)tolower((unsigned char)infos[k].name[i]);
            name[i] = (char)0;
            if (strstr(name, needle) != NULL) {
                *out_id = infos[k].id;
                found = 1;
            }
        }
    }
    ma_context_uninit(&ctx);
    return found;
}

/* 设备能力检查：请求的通道数/采样率不原生支持时打印转换告警（LOG 行）。 */
static void host_check_caps(ma_device_type type, const ma_device_id* p_id,
                            uint32_t channels, uint32_t sample_rate, const char* side) {
    ma_context ctx;
    if (ma_context_init(NULL, 0, NULL, &ctx) != MA_SUCCESS) {
        printf("LOG %s device caps unknown: cannot init audio context\n", side);
        return;
    }
    ma_device_id default_id;
    const ma_device_id* query_id = p_id;
    if (query_id == NULL) {
        ma_device_info* p_play = NULL;
        ma_device_info* p_cap = NULL;
        ma_uint32 n_play = 0, n_cap = 0;
        if (ma_context_get_devices(&ctx, &p_play, &n_play, &p_cap, &n_cap) == MA_SUCCESS) {
            ma_device_info* infos = (type == ma_device_type_playback) ? p_play : p_cap;
            ma_uint32 count = (type == ma_device_type_playback) ? n_play : n_cap;
            for (ma_uint32 i = 0; i < count; ++i) {
                if (infos[i].isDefault) {
                    default_id = infos[i].id;
                    query_id = &default_id;
                    break;
                }
            }
        }
    }
    ma_device_info info;
    if (ma_context_get_device_info(&ctx, type, query_id, &info) != MA_SUCCESS) {
        ma_context_uninit(&ctx);
        printf("LOG %s device caps unknown: info unavailable\n", side);
        return;
    }
    ma_context_uninit(&ctx);
    if (info.nativeDataFormatCount == 0) {
        printf("LOG %s device: %s (no native format info, conversion automatic)\n",
               side, info.name);
        return;
    }
    int native = 0, ch_any = 0, rate_any = 0;
    for (ma_uint32 i = 0; i < info.nativeDataFormatCount; ++i) {
        ma_uint32 ch = info.nativeDataFormats[i].channels;
        ma_uint32 sr = info.nativeDataFormats[i].sampleRate;
        if (ch == 0 || ch == channels) ch_any = 1;
        if (sr == 0 || sr == sample_rate) rate_any = 1;
        if ((ch == 0 || ch == channels) && (sr == 0 || sr == sample_rate)) {
            native = 1;
            break;
        }
    }
    if (native) {
        printf("LOG %s device native: %s\n", side, info.name);
    } else {
        printf("LOG WARN %s device will convert: %s (channels=%u%s, rate=%uHz%s)\n",
               side, info.name, channels, ch_any ? " native" : " converted",
               sample_rate, rate_any ? " native" : " converted");
    }
}

/* ------------------------------------------------------------------ 回调 */

/* duplex（麦克风）与单设备模式共用：设备周期可能大于图块长，按块分片处理。 */
static void host_data_callback(ma_device* dev, void* p_out, const void* p_in,
                               ma_uint32 frame_count) {
    HostContext* h = (HostContext*)dev->pUserData;
    float* out = (float*)p_out;
    const float* in = (const float*)p_in;
    for (ma_uint32 done = 0; done < frame_count; done += HOST_BS) {
        uint32_t n = (frame_count - done) < HOST_BS ? (frame_count - done) : HOST_BS;
        if (h->in_buf && in) {
            memcpy(h->in_buf->data, in + (size_t)done * HOST_IN_CH,
                   (size_t)n * HOST_IN_CH * sizeof(float));
            h->in_buf->frame_count = n;
        }
        if (orpheus_generated_process(n) != ORPHEUS_OK) {
            /* 实时线程内不打印（红线）；保持音频连续，错误由调用方在块边界自查 */
        }
        if (h->out_buf && out) {
            memcpy(out + (size_t)done * HOST_OUT_CH, h->out_buf->data,
                   (size_t)n * HOST_OUT_CH * sizeof(float));
        } else if (out) {
            memset(out + (size_t)done * HOST_OUT_CH, 0,
                   (size_t)n * HOST_OUT_CH * sizeof(float));
        }
    }
}

/* 异步桥采集侧：任意采集源（含 loopback）推入环形缓冲。 */
static void host_rb_capture_callback(ma_device* dev, void* p_out, const void* p_in,
                                     ma_uint32 frame_count) {
    (void)p_out;
    HostContext* h = (HostContext*)dev->pUserData;
    if (!h->rb || !p_in) return;
    ma_uint32 writable = frame_count;
    void* w = NULL;
    if (ma_pcm_rb_acquire_write(h->rb, &writable, &w) == MA_SUCCESS && writable > 0) {
        memcpy(w, p_in, (size_t)writable * HOST_IN_CH * sizeof(float));
        ma_pcm_rb_commit_write(h->rb, writable);
        if (writable < frame_count) h->overruns++;
    } else {
        h->overruns++;
    }
}

/* 异步桥播放侧：播放设备为主时钟，从环形缓冲拉取输入。 */
static void host_rb_playback_callback(ma_device* dev, void* p_out, const void* p_in,
                                      ma_uint32 frame_count) {
    (void)p_in;
    HostContext* h = (HostContext*)dev->pUserData;
    float* out = (float*)p_out;

    /* 预充：环形缓冲起始为空，等采集预填到水位再消费，
     * 否则播放在近空缓冲上饥饿，即使速率匹配也会持续欠载。 */
    if (h->rb && h->prime_target > 0 && !h->primed) {
        if (ma_pcm_rb_available_read(h->rb) < h->prime_target) {
            memset(out, 0, (size_t)frame_count * HOST_OUT_CH * sizeof(float));
            return;
        }
        h->primed = 1;
    }

    for (ma_uint32 done = 0; done < frame_count; done += HOST_BS) {
        uint32_t n = (frame_count - done) < HOST_BS ? (frame_count - done) : HOST_BS;
        if (h->in_buf) {
            float* buf = (float*)h->in_buf->data;
            ma_uint32 readable = n;
            void* r = NULL;
            ma_uint32 got = 0;
            if (h->rb &&
                ma_pcm_rb_acquire_read(h->rb, &readable, &r) == MA_SUCCESS && readable > 0) {
                memcpy(buf, r, (size_t)readable * HOST_IN_CH * sizeof(float));
                ma_pcm_rb_commit_read(h->rb, readable);
                got = readable;
            }
            if (got < n) {
                h->underruns++;
                memset(buf + (size_t)got * HOST_IN_CH, 0,
                       (size_t)(n - got) * HOST_IN_CH * sizeof(float));
            }
            h->in_buf->frame_count = n;
        }
        if (orpheus_generated_process(n) != ORPHEUS_OK) {
            /* 同上：实时线程内不打印 */
        }
        if (h->out_buf && out) {
            memcpy(out + (size_t)done * HOST_OUT_CH, h->out_buf->data,
                   (size_t)n * HOST_OUT_CH * sizeof(float));
        } else if (out) {
            memset(out + (size_t)done * HOST_OUT_CH, 0,
                   (size_t)n * HOST_OUT_CH * sizeof(float));
        }
    }
}

/* ------------------------------------------------------------------ 探针 */

static void host_report_probes(void) {
    size_t count = orpheus_control_probe_count();
    for (size_t i = 0; i < count; ++i) {
        const char* node = NULL;
        const char* key = NULL;
        OrpheusValue v;
        if (orpheus_control_probe_get(i, &node, &key, &v) != 0) continue;
        if (v.type == ORPHEUS_VALUE_FLOAT) {
            printf("PROBE %s %s %g\n", node, key, (double)v.value.f32);
        } else if (v.type == ORPHEUS_VALUE_INT) {
            printf("PROBE %s %s %d\n", node, key, (int)v.value.i32);
        } else if (v.type == ORPHEUS_VALUE_BOOL) {
            printf("PROBE %s %s %d\n", node, key, v.value.b ? 1 : 0);
        } else if (v.type == ORPHEUS_VALUE_STRING) {
            /* 复合/结构化探针（波形/频谱 JSON）：整行 */
            printf("PROBE_JSON %s %s %s\n", node, key, v.value.str ? v.value.str : "");
        }
    }
}

typedef struct {
    HostContext* host;
    volatile int* running;
} ProbeThreadArgs;

static int host_probe_thread(void* arg) {
    ProbeThreadArgs* a = (ProbeThreadArgs*)arg;
    uint32_t last_u = 0, last_o = 0;
    int ticks = 0;
    int priming_warned = 0, primed_logged = 0;
    while (*a->running) {
        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = 200 * 1000 * 1000;
        thrd_sleep(&ts, NULL);
        if (!*a->running) break;
        host_report_probes();
        /* 环形缓冲水位 + 欠/溢计数 -> UI 仪表（PROBE_JSON __host__） */
        {
            uint32_t lvl = a->host->rb ? ma_pcm_rb_available_read(a->host->rb) : 0;
            printf("PROBE_JSON __host__ rb {\"level\":%u,\"capacity\":%u,"
                   "\"primed\":%s,\"underruns\":%u,\"overruns\":%u,\"bridge\":%s}\n",
                   lvl, a->host->rb_capacity,
                   a->host->primed ? "true" : "false",
                   a->host->underruns, a->host->overruns,
                   a->host->rb ? "true" : "false");
        }
        /* 每秒一次：水位问题告警与建议 */
        if (++ticks % 5 == 0) {
            uint32_t u = a->host->underruns;
            uint32_t o = a->host->overruns;
            if (u != last_u) {
                printf("LOG WARN 播放欠载 x%u/s：播放设备取数不足（出现杂音/哒哒声）。建议：增大 buffer_size，"
                       "或检查采集设备是否正常供数/改用同一设备时钟\n", u - last_u);
            }
            if (o != last_o) {
                printf("LOG WARN 采集溢出 x%u/s：输入数据堆积被丢弃（采集与播放时钟漂移）。"
                       "建议：增大 buffer_size，或让输入输出共用同一设备/时钟\n", o - last_o);
            }
            if (a->host->rb) {
                if (!a->host->primed) {
                    if (!priming_warned && ticks >= 10) {
                        priming_warned = 1;
                        printf("LOG WARN 缓冲预充不足：采集 2 秒内未填满水位（loopback 目标可能未在播放，"
                               "或采集设备异常）。播放暂输出静音，等待采集供数。\n");
                    }
                } else if (!primed_logged) {
                    primed_logged = 1;
                    printf("LOG 缓冲预充完成，开始播放\n");
                }
            }
            last_u = u;
            last_o = o;
        }
    }
    return 0;
}

/* ------------------------------------------------------------ 控制协议 */

static const char* host_hex_digits = "0123456789abcdef";

static int host_from_hex(const char* hx, uint8_t* out, size_t* out_len) {
    size_t n = strlen(hx);
    size_t i;
    if (n % 2 != 0) return -1;
    *out_len = 0;
    for (i = 0; i < n; i += 2) {
        int hi = -1, lo = -1;
        char c1 = hx[i], c2 = hx[i + 1];
        if (c1 >= '0' && c1 <= '9') hi = c1 - '0';
        else if (c1 >= 'a' && c1 <= 'f') hi = c1 - 'a' + 10;
        else if (c1 >= 'A' && c1 <= 'F') hi = c1 - 'A' + 10;
        if (c2 >= '0' && c2 <= '9') lo = c2 - '0';
        else if (c2 >= 'a' && c2 <= 'f') lo = c2 - 'a' + 10;
        else if (c2 >= 'A' && c2 <= 'F') lo = c2 - 'A' + 10;
        if (hi < 0 || lo < 0) return -1;
        out[(*out_len)++] = (uint8_t)((hi << 4) | lo);
    }
    return 0;
}

/* 值打印：与 rt_host 的 VALUE/RVALUE 行格式一致 */
static void host_print_value(const OrpheusValue* v) {
    if (v->type == ORPHEUS_VALUE_FLOAT) printf("%g", (double)v->value.f32);
    else if (v->type == ORPHEUS_VALUE_INT) printf("%d", (int)v->value.i32);
    else if (v->type == ORPHEUS_VALUE_BOOL) printf("%d", v->value.b ? 1 : 0);
    else if (v->type == ORPHEUS_VALUE_STRING) printf("%s", v->value.str ? v->value.str : "");
    else printf("?");
}

/* 文本值解析：纯数值 -> FLOAT，否则 STRING（与 rt_host 一致） */
static OrpheusValue host_parse_value(const char* raw) {
    OrpheusValue v;
    char* end = NULL;
    float f = strtof(raw, &end);
    if (end != raw && end && *end == (char)0) {
        v.type = ORPHEUS_VALUE_FLOAT;
        v.value.f32 = f;
    } else {
        v.type = ORPHEUS_VALUE_STRING;
        v.value.str = raw;
    }
    return v;
}

static const OrpheusIdEntry* host_find_id(uint32_t id) {
    size_t count = 0;
    const OrpheusIdEntry* map = orpheus_id_map(&count);
    for (size_t i = 0; i < count; ++i) {
        if (map[i].id == id) return &map[i];
    }
    return NULL;
}

/* GETBULK node/key -> id：经 id_map 的 node/key 字段定位（读回行格式对齐 rt_host） */
static uint32_t host_lookup_bulk_id(const char* node, const char* key) {
    size_t count = 0;
    const OrpheusIdEntry* map = orpheus_id_map(&count);
    for (size_t i = 0; i < count; ++i) {
        if (map[i].node && map[i].key &&
            strcmp(map[i].node, node) == 0 && strcmp(map[i].key, key) == 0)
            return map[i].id;
    }
    return 0;
}

static void host_print_resolved(const OrpheusIdEntry* e) {
    /* 格式对齐 RtSession._parse_resolved_line：RESOLVED <hex> <kind> <form> key=value... */
    void* arena = orpheus_arena_base();
    const void* base = arena ? (const char*)arena + e->arena_offset : NULL;
    printf("RESOLVED 0x%08X %u %u type=%u count=%u bytes=%u module=%u slot=%u "
           "base=%p offset=%u node=%s key=%s name=%s\n",
           e->id, e->kind, e->form, e->type, e->count, (unsigned)e->byte_size,
           e->module_id, e->slot, base, (unsigned)e->arena_offset,
           e->node ? e->node : "", e->key ? e->key : "", e->name ? e->name : "");
}

static void host_control_loop(volatile int* running) {
    char line[4096];
    while (*running && fgets(line, sizeof(line), stdin)) {
        char* p = line;
        while (*p == ' ' || *p == '\t') ++p;
        size_t len = strlen(p);
        while (len > 0 && (p[len - 1] == '\n' || p[len - 1] == '\r')) p[--len] = (char)0;
        if (len == 0) break;  /* 空行 = 停止（与 rt_host 一致） */

        char* save = NULL;
        char* cmd = strtok_s(p, " \t", &save);
        if (!cmd) break;

        if (strcmp(cmd, "STOP") == 0) {
            break;
        } else if (strcmp(cmd, "SET") == 0) {
            char* node = strtok_s(NULL, " \t", &save);
            char* param = strtok_s(NULL, " \t", &save);
            char* raw = save;
            while (raw && (*raw == ' ' || *raw == '\t')) ++raw;
            if (!node || !param || !raw || !raw[0]) {
                printf("ERR SET %s %s\n", node ? node : "?", param ? param : "?");
                continue;
            }
            OrpheusValue v = host_parse_value(raw);
            int r = orpheus_control_set_value(node, param, v);
            printf("%s SET %s %s\n", r == 0 ? "OK" : "ERR", node, param);
        } else if (strcmp(cmd, "GET") == 0) {
            char* node = strtok_s(NULL, " \t", &save);
            char* param = strtok_s(NULL, " \t", &save);
            OrpheusValue v;
            if (node && param && orpheus_control_get_value(node, param, &v) == 0) {
                printf("VALUE %s %s ", node, param);
                host_print_value(&v);
                printf("\n");
            } else {
                printf("ERR GET %s %s\n", node ? node : "?", param ? param : "?");
            }
        } else if (strcmp(cmd, "BULK") == 0) {
            char* node = strtok_s(NULL, " \t", &save);
            char* key = strtok_s(NULL, " \t", &save);
            char* ns = strtok_s(NULL, " \t", &save);
            if (!node || !key || !ns) {
                printf("ERR BULK %s %s\n", node ? node : "?", key ? key : "?");
                continue;
            }
            size_t n = (size_t)atoi(ns);
            float vals[1024];
            size_t got = 0;
            char* t;
            while (got < n && got < 1024 && (t = strtok_s(NULL, " \t", &save)) != NULL)
                vals[got++] = (float)atof(t);
            if (got != n) {
                printf("ERR BULK %s %s\n", node, key);
                continue;
            }
            int r = orpheus_control_write_bulk(node, key, vals, got);
            printf("%s BULK %s %s\n", r == 0 ? "OK" : "ERR", node, key);
        } else if (strcmp(cmd, "RESOLVE") == 0 || strcmp(cmd, "MAP") == 0) {
            if (strcmp(cmd, "RESOLVE") == 0) {
                char* raw = strtok_s(NULL, " \t", &save);
                uint32_t id = raw ? (uint32_t)strtoul(raw, NULL, 0) : 0;
                const OrpheusIdEntry* e = host_find_id(id);
                if (e) host_print_resolved(e);
                else printf("ERR RESOLVE 0x%08X\n", id);
            } else {
                size_t count = 0;
                const OrpheusIdEntry* map = orpheus_id_map(&count);
                for (size_t i = 0; i < count; ++i) host_print_resolved(&map[i]);
            }
        } else if (strcmp(cmd, "RW") == 0) {
            char* raw_id = strtok_s(NULL, " \t", &save);
            char* raw = save;
            while (raw && (*raw == ' ' || *raw == '\t')) ++raw;
            uint32_t id = raw_id ? (uint32_t)strtoul(raw_id, NULL, 0) : 0;
            if (!raw_id || !raw || !raw[0]) {
                printf("ERR RW 0x%08X\n", id);
                continue;
            }
            OrpheusValue v = host_parse_value(raw);
            int r = orpheus_control_set_value_id(id, v);
            printf("%s RW 0x%08X\n", r == 0 ? "OK" : "ERR", id);
        } else if (strcmp(cmd, "RR") == 0) {
            char* raw_id = strtok_s(NULL, " \t", &save);
            uint32_t id = raw_id ? (uint32_t)strtoul(raw_id, NULL, 0) : 0;
            OrpheusValue v;
            if (raw_id && orpheus_control_get_value_id(id, &v) == 0) {
                printf("RVALUE 0x%08X ", id);
                host_print_value(&v);
                printf("\n");
            } else {
                printf("ERR RR 0x%08X\n", id);
            }
        } else if (strcmp(cmd, "RWB") == 0) {
            char* raw_id = strtok_s(NULL, " \t", &save);
            char* ns = strtok_s(NULL, " \t", &save);
            uint32_t id = raw_id ? (uint32_t)strtoul(raw_id, NULL, 0) : 0;
            if (!raw_id || !ns) {
                printf("ERR RWB 0x%08X\n", id);
                continue;
            }
            size_t n = (size_t)atoi(ns);
            float vals[1024];
            size_t got = 0;
            char* t;
            while (got < n && got < 1024 && (t = strtok_s(NULL, " \t", &save)) != NULL)
                vals[got++] = (float)atof(t);
            if (got != n) {
                printf("ERR RWB 0x%08X\n", id);
                continue;
            }
            int r = orpheus_control_write_bulk_id(id, vals, got);
            printf("%s RWB 0x%08X\n", r == 0 ? "OK" : "ERR", id);
        } else if (strcmp(cmd, "GETBULK") == 0 || strcmp(cmd, "RGB") == 0) {
            uint32_t id = 0;
            if (strcmp(cmd, "RGB") == 0) {
                char* raw_id = strtok_s(NULL, " \t", &save);
                id = raw_id ? (uint32_t)strtoul(raw_id, NULL, 0) : 0;
            } else {
                char* node = strtok_s(NULL, " \t", &save);
                char* key = strtok_s(NULL, " \t", &save);
                if (node && key) id = host_lookup_bulk_id(node, key);
            }
            size_t n = id ? orpheus_control_bulk_count_id(id) : 0;
            if (n == 0 || n > 4096) {
                printf("ERR GETBULK 0x%08X\n", id);
                continue;
            }
            float vals[4096];
            if (orpheus_control_get_bulk_id(id, vals, n) == 0) {
                printf("BULKVALUE 0x%08X", id);
                for (size_t k = 0; k < n; ++k) printf(" %g", (double)vals[k]);
                printf("\n");
            } else {
                printf("ERR GETBULK 0x%08X\n", id);
            }
        } else if (strcmp(cmd, "MSG") == 0) {
            char* hx = strtok_s(NULL, " \t", &save);
            uint8_t in[4096];
            size_t in_len = 0;
            if (!hx || host_from_hex(hx, in, &in_len) != 0) {
                printf("ERR MSG hex\n");
                continue;
            }
            uint8_t out[65536];
            size_t out_len = 0;
            if (orpheus_control_message(in, in_len, out, sizeof(out), &out_len) != 0) {
                printf("ERR MSG dispatch\n");
                continue;
            }
            if (out_len == 0) {
                printf("MSGNONE\n");
            } else {
                size_t k;
                printf("MSGRSP ");
                for (k = 0; k < out_len; ++k) {
                    printf("%c%c", host_hex_digits[out[k] >> 4],
                           host_hex_digits[out[k] & 0xF]);
                }
                printf("\n");
            }
        }
        /* 未知命令静默忽略（与 rt_host 一致：不打印，避免干扰日志解析） */
    }
    *running = 0;
}

/* ------------------------------------------------------------------ main */

int main(void) {
    /* stdout 无缓冲：LOG/PROBE 行必须立即到达父进程（与 rt_host 一致） */
    setvbuf(stdout, NULL, _IONBF, 0);

    if (orpheus_generated_init(HOST_SR, HOST_BS) != ORPHEUS_OK) {
        fprintf(stderr, "init failed\n");
        return 1;
    }

    HostContext host;
    memset(&host, 0, sizeof(host));
    host.in_buf = orpheus_host_device_in_buffer();
    host.out_buf = orpheus_host_device_out_buffer();
    host.underruns = 0;
    host.overruns = 0;
    host.primed = 0;

    const int has_in = ORPHEUS_HOST_HAS_IN;
    const int has_out = ORPHEUS_HOST_HAS_OUT;
    const int loopback = ORPHEUS_HOST_LOOPBACK;

    ma_device_id in_id, out_id;
    ma_device_id* p_in_id =
        host_find_device_id(loopback ? ma_device_type_playback : ma_device_type_capture,
                            ORPHEUS_HOST_IN_DEVICE, &in_id) ? &in_id : NULL;
    ma_device_id* p_out_id =
        host_find_device_id(ma_device_type_playback, ORPHEUS_HOST_OUT_DEVICE, &out_id)
            ? &out_id : NULL;
    if (has_in && ORPHEUS_HOST_IN_DEVICE[0] && !p_in_id) {
        fprintf(stderr, "Input device not found: %s\n", ORPHEUS_HOST_IN_DEVICE);
        return 1;
    }
    if (has_out && ORPHEUS_HOST_OUT_DEVICE[0] && !p_out_id) {
        fprintf(stderr, "Output device not found: %s\n", ORPHEUS_HOST_OUT_DEVICE);
        return 1;
    }

    /* 设备能力校验：需要 miniaudio 转换时告警；真正不支持会在 init 失败（错误）。 */
    if (has_in) {
        host_check_caps(loopback ? ma_device_type_playback : ma_device_type_capture,
                        p_in_id, HOST_IN_CH, HOST_SR, "input");
    }
    if (has_out) {
        host_check_caps(ma_device_type_playback, p_out_id, HOST_OUT_CH, HOST_SR, "output");
    }

    ma_pcm_rb rb;
    int rb_inited = 0;
    ma_device cap_device, play_device;
    int cap_inited = 0, play_inited = 0;
    const char* mode = "?";

    /* 拓扑选择（与 rt_host 一致） */
    const int async_bridge =
        has_in && has_out &&
        (loopback || ORPHEUS_HOST_IN_DEVICE[0] || ORPHEUS_HOST_OUT_DEVICE[0]);

    if (has_in && has_out && !async_bridge) {
        /* 双默认设备：单 duplex，同一时钟域，最低延迟 */
        ma_device_config cfg = ma_device_config_init(ma_device_type_duplex);
        cfg.capture.pDeviceID = p_in_id;
        cfg.capture.format = ma_format_f32;
        cfg.capture.channels = HOST_IN_CH;
        cfg.playback.pDeviceID = p_out_id;
        cfg.playback.format = ma_format_f32;
        cfg.playback.channels = HOST_OUT_CH;
        cfg.sampleRate = HOST_SR;
        cfg.periodSizeInFrames = host_period_frames();
        cfg.dataCallback = host_data_callback;
        cfg.pUserData = &host;
        if (ma_device_init(NULL, &cfg, &play_device) != MA_SUCCESS) {
            fprintf(stderr, "Failed to initialize audio device\n");
            return 1;
        }
        play_inited = 1;
        mode = "duplex";
        if (ma_device_start(&play_device) != MA_SUCCESS) {
            fprintf(stderr, "Failed to start audio device\n");
            ma_device_uninit(&play_device);
            return 1;
        }
    } else if (has_in && has_out) {
        /* 异步桥：loopback 或异源采集/播放设备（时钟解耦 + 水位监控） */
        uint32_t rb_frames =
            ORPHEUS_HOST_BUFFER_FRAMES ? (uint32_t)ORPHEUS_HOST_BUFFER_FRAMES
                                       : (HOST_SR / 10);
        if (ma_pcm_rb_init(ma_format_f32, HOST_IN_CH, rb_frames, NULL, NULL, &rb) !=
            MA_SUCCESS) {
            fprintf(stderr, "Failed to init ring buffer\n");
            return 1;
        }
        rb_inited = 1;
        host.rb = &rb;
        host.rb_capacity = rb_frames;
        host.prime_target = rb_frames / 3;  /* 预充水位 */

        ma_device_config cap_cfg = ma_device_config_init(
            loopback ? ma_device_type_loopback : ma_device_type_capture);
        cap_cfg.capture.pDeviceID = p_in_id;  /* loopback 目标 = 被监听的播放设备 */
        cap_cfg.capture.format = ma_format_f32;
        cap_cfg.capture.channels = HOST_IN_CH;
        cap_cfg.sampleRate = HOST_SR;
        cap_cfg.periodSizeInFrames = host_period_frames();
        cap_cfg.dataCallback = host_rb_capture_callback;
        cap_cfg.pUserData = &host;
        if (ma_device_init(NULL, &cap_cfg, &cap_device) != MA_SUCCESS) {
            fprintf(stderr, "Failed to initialize capture device\n");
            ma_pcm_rb_uninit(&rb);
            return 1;
        }
        cap_inited = 1;

        ma_device_config play_cfg = ma_device_config_init(ma_device_type_playback);
        play_cfg.playback.pDeviceID = p_out_id;
        play_cfg.playback.format = ma_format_f32;
        play_cfg.playback.channels = HOST_OUT_CH;
        play_cfg.sampleRate = HOST_SR;
        play_cfg.periodSizeInFrames = host_period_frames();
        play_cfg.dataCallback = host_rb_playback_callback;
        play_cfg.pUserData = &host;
        if (ma_device_init(NULL, &play_cfg, &play_device) != MA_SUCCESS) {
            fprintf(stderr, "Failed to initialize playback device\n");
            ma_device_uninit(&cap_device);
            ma_pcm_rb_uninit(&rb);
            return 1;
        }
        play_inited = 1;
        mode = "async-bridge";
        if (ma_device_start(&play_device) != MA_SUCCESS ||
            ma_device_start(&cap_device) != MA_SUCCESS) {
            fprintf(stderr, "Failed to start audio devices\n");
            ma_device_uninit(&play_device);
            ma_device_uninit(&cap_device);
            ma_pcm_rb_uninit(&rb);
            return 1;
        }
    } else {
        /* 单设备：它即图时钟（仅播放 / 仅采集 / 仅 loopback） */
        ma_device_config cfg = ma_device_config_init(
            has_out ? ma_device_type_playback
                    : (loopback ? ma_device_type_loopback : ma_device_type_capture));
        if (has_out) {
            cfg.playback.pDeviceID = p_out_id;
            cfg.playback.format = ma_format_f32;
            cfg.playback.channels = HOST_OUT_CH;
        } else {
            cfg.capture.pDeviceID = p_in_id;
            cfg.capture.format = ma_format_f32;
            cfg.capture.channels = HOST_IN_CH;
        }
        cfg.sampleRate = HOST_SR;
        cfg.periodSizeInFrames = host_period_frames();
        cfg.dataCallback = host_data_callback;
        cfg.pUserData = &host;
        if (ma_device_init(NULL, &cfg, &play_device) != MA_SUCCESS) {
            fprintf(stderr, "Failed to initialize audio device\n");
            return 1;
        }
        play_inited = 1;
        mode = "device-clock";
        if (ma_device_start(&play_device) != MA_SUCCESS) {
            fprintf(stderr, "Failed to start audio device\n");
            ma_device_uninit(&play_device);
            return 1;
        }
    }

    printf("LOG host_win running (generated, in=%s, out=%s, mode=%s, "
           "in_channels=%u, out_channels=%u, sample_rate=%u, block_size=%u)\n",
           has_in ? (loopback ? "loopback" : "mic") : "none",
           has_out ? "playback" : "none", mode,
           HOST_IN_CH, HOST_OUT_CH, HOST_SR, HOST_BS);
    if (play_inited) {
        printf("LOG device period: playback=%u frames",
               (unsigned)play_device.playback.internalPeriodSizeInFrames);
        if (cap_inited) {
            printf(", capture=%u frames",
                   (unsigned)cap_device.capture.internalPeriodSizeInFrames);
        } else if (has_in) {
            printf(", capture=%u frames",
                   (unsigned)play_device.capture.internalPeriodSizeInFrames);
        }
        printf("\n");
    }

    volatile int running = 1;
    ProbeThreadArgs probe_args;
    probe_args.host = &host;
    probe_args.running = &running;
    thrd_t probe_thr;
    int probe_started =
        thrd_create(&probe_thr, host_probe_thread, &probe_args) == thrd_success;

    host_control_loop(&running);  /* STOP / 空行 / stdin EOF 返回 */

    running = 0;
    if (probe_started) thrd_join(probe_thr, NULL);

    if (play_inited) ma_device_uninit(&play_device);
    if (cap_inited) ma_device_uninit(&cap_device);
    if (rb_inited) ma_pcm_rb_uninit(&rb);
    orpheus_generated_teardown();
    printf("LOG host_win stopped\n");
    return 0;
}
