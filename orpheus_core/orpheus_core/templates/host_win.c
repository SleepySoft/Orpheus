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
 * 外部控制统一使用 stdin/stdout LengthPrefix Bridge；stdout 只承载二进制帧，
 * HELLO/IDENTITY/MAP/STOP 与数据点 CALL 由共享 Endpoint 处理，诊断写 stderr。
 *
 * 设备拓扑（与 rt_host 一致）：
 *   采集+播放均为默认设备         -> 单 duplex 设备（同一时钟域，最低延迟）
 *   采集+播放但指定设备/loopback  -> 异步桥（capture -> 环形缓冲 -> playback 主时钟），
 *                                    解耦异源时钟并通过水位上报欠载/溢出
 *   仅播放 / 仅采集               -> 单设备即图时钟
 */

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include "orpheus_graph.h"
#include "orpheus_bridge_generated.h"
#include "orpheus_host_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* stdout 专用于长度前缀 Bridge 帧；所有宿主诊断写 stderr。 */
#define printf(...) fprintf(stderr, __VA_ARGS__)

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

/* ------------------------------------------------------------------ main */

int main(int argc, char** argv) {
    /* stdout 无缓冲，保证 Bridge 帧立即到达父进程。 */
    setvbuf(stdout, NULL, _IONBF, 0);

    OrpheusGeneratedBridgeConfig bridge;
    memset(&bridge, 0, sizeof(bridge));
    bridge.transport = ORPHEUS_BRIDGE_TRANSPORT_STDIO;
    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (strcmp(arg, "--bridge") == 0 && i + 1 < argc) {
            const char* value = argv[++i];
            if (strcmp(value, "stdio") == 0) {
                bridge.transport = ORPHEUS_BRIDGE_TRANSPORT_STDIO;
            } else if (strcmp(value, "pipe") == 0) {
                bridge.transport = ORPHEUS_BRIDGE_TRANSPORT_PIPE;
            } else if (strcmp(value, "tcp") == 0) {
                bridge.transport = ORPHEUS_BRIDGE_TRANSPORT_TCP;
            } else {
                fprintf(stderr, "Unknown bridge transport: %s\n", value);
                return 1;
            }
        } else if (strcmp(arg, "--pipe-name") == 0 && i + 1 < argc) {
            bridge.pipe_name = argv[++i];
        } else if (strcmp(arg, "--host") == 0 && i + 1 < argc) {
            bridge.host = argv[++i];
        } else if (strcmp(arg, "--port") == 0 && i + 1 < argc) {
            const unsigned port = (unsigned)strtoul(argv[++i], NULL, 0);
            if (port > 65535u) {
                fprintf(stderr, "Invalid TCP port: %u\n", port);
                return 1;
            }
            bridge.port = (uint16_t)port;
        } else if (strcmp(arg, "--endpoint-file") == 0 && i + 1 < argc) {
            bridge.endpoint_file = argv[++i];
        } else {
            fprintf(stderr, "Usage: %s [--bridge stdio|pipe|tcp]"
                " [--pipe-name NAME] [--host HOST] [--port PORT]"
                " [--endpoint-file PATH]\n", argv[0]);
            return 1;
        }
    }

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

    /* 音频由设备回调推进；主线程只服务标准二进制 Bridge，STOP 后返回。 */
    char bridge_endpoint[512];
    int bridge_result = orpheus_generated_bridge_serve(
        &bridge, bridge_endpoint, sizeof(bridge_endpoint));

    if (play_inited) ma_device_uninit(&play_device);
    if (cap_inited) ma_device_uninit(&cap_device);
    if (rb_inited) ma_pcm_rb_uninit(&rb);
    orpheus_generated_teardown();
    printf("LOG host_win stopped\n");
    return bridge_result == ORPHEUS_OK ? 0 : 1;
}
