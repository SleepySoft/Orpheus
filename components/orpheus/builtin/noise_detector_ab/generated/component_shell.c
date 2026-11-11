#include "component_state.h"
#include <string.h>
#include <math.h>

static const OrpheusPort ports[] = {
    {.id="ref", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_AUDIO, .sample_format=ORPHEUS_FORMAT_F32, .channels=0, .sample_rate=0, .block_size=((double)(0) * (double)(0)), .is_variable=true, .channels_param="channels"},
    {.id="in", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_AUDIO, .sample_format=ORPHEUS_FORMAT_F32, .channels=0, .sample_rate=0, .block_size=((double)(0) * (double)(0)), .is_variable=true, .channels_param="channels"},
    {.id="out", .direction=ORPHEUS_PORT_OUTPUT, .type=ORPHEUS_PORT_AUDIO, .sample_format=ORPHEUS_FORMAT_F32, .channels=0, .sample_rate=0, .block_size=((double)(0) * (double)(0)), .is_variable=true, .channels_param="channels"},
};

static const OrpheusParameter params[] = {
    {.id="channels", .name="通道数", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=2}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=true, .min_i32=1, .max_i32=32},
    {.id="fft_size", .name="FFT窗口点数", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=128}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=true, .min_i32=4, .max_i32=4096},
    {.id="batches", .name="顺序窗口数", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=1}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=true, .min_i32=1, .max_i32=64},
    {.id="smoothing", .name="平滑窗口数", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=16}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=false, .min_i32=1, .max_i32=128},
    {.id="threshold_db", .name="失真噪声阈值", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=-4.000000000e+01f}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="dB", .readback=true, .persistent=true, .affects_signature=false, .min_f32=-1.200000000e+02f, .max_f32=0.000000000e+00f},
    {.id="time_thres", .name="时域残差阈值", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=2.000000000e-02f}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=false, .min_f32=0.000000000e+00f, .max_f32=1.000000000e+00f},
    {.id="thd_n_db", .name="失真+噪声", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=0.000000000e+00f}, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="noise_frames", .name="噪声窗口数", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=0}, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="noise_ratio", .name="噪声占比", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=0.000000000e+00f}, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="clicks", .name="突刺计数", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=0}, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="residue_pk", .name="残差峰值", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=0.000000000e+00f}, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="detail", .name="频带明细", .type=ORPHEUS_VALUE_STRING, .default_value={.type=ORPHEUS_VALUE_STRING, .value.str="{}"}, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .readback=true, .persistent=true, .affects_signature=false},
};

static const OrpheusComponentDescriptor descriptor = {
    .id="orpheus.builtin.noise_detector_ab", .version="1.0.0", .abi_version=7,
    .ports=ports, .port_count=3,
    .params=params, .param_count=12,
    .state_size=sizeof(orpheus_builtin_noise_detector_ab_State), .alignment=_Alignof(orpheus_builtin_noise_detector_ab_State),
    .scratch_size=0, .latency_samples=0,
    .realtime_safe=true, .supports_inplace=false
};
static const OrpheusComponentDescriptor* get_descriptor(void) { return &descriptor; }

static void snapshot_parameters(const orpheus_builtin_noise_detector_ab_State* s, orpheus_builtin_noise_detector_ab_Parameters* p) {
    (void)s; memset(p, 0, sizeof(*p));
    p->channels=s->channels;
    p->fft_size=s->fft_size;
    p->batches=s->batches;
    p->smoothing=s->smoothing;
    p->threshold_db=s->threshold_db;
    p->time_thres=s->time_thres;
    p->thd_n_db=s->thd_n_db;
    p->noise_frames=s->noise_frames;
    p->noise_ratio=s->noise_ratio;
    p->clicks=s->clicks;
    p->residue_pk=s->residue_pk;
    memcpy(p->detail, s->detail, sizeof(p->detail));
}
static void commit_parameters(orpheus_builtin_noise_detector_ab_State* s, const orpheus_builtin_noise_detector_ab_Parameters* p) {
    (void)s; (void)p;
    s->channels=p->channels;
    s->fft_size=p->fft_size;
    s->batches=p->batches;
    s->smoothing=p->smoothing;
    s->threshold_db=p->threshold_db;
    s->time_thres=p->time_thres;
    s->thd_n_db=p->thd_n_db;
    s->noise_frames=p->noise_frames;
    s->noise_ratio=p->noise_ratio;
    s->clicks=p->clicks;
    s->residue_pk=p->residue_pk;
    memcpy(s->detail, p->detail, sizeof(s->detail));
}
static void default_parameters(orpheus_builtin_noise_detector_ab_Parameters* p) {
    memset(p, 0, sizeof(*p));
    p->channels=2;
    p->fft_size=128;
    p->batches=1;
    p->smoothing=16;
    p->threshold_db=-4.000000000e+01f;
    p->time_thres=2.000000000e-02f;
    p->thd_n_db=0.000000000e+00f;
    p->noise_frames=0;
    p->noise_ratio=0.000000000e+00f;
    p->clicks=0;
    p->residue_pk=0.000000000e+00f;
    memcpy(p->detail, "{}", 3);
}
static int valid_utf8_string(const char* text, size_t capacity, size_t* length) {
    if (!text) return ORPHEUS_ERR_INVALID_ARG;
    size_t i=0;
    while (i<capacity) {
        unsigned char c=(unsigned char)text[i];
        if (!c) { *length=i; return ORPHEUS_OK; }
        if (c<0x80) { ++i; continue; }
        size_t n=(c>=0xC2 && c<=0xDF) ? 2 : (c>=0xE0 && c<=0xEF) ? 3 :
                 (c>=0xF0 && c<=0xF4) ? 4 : 0;
        if (!n || n>capacity-i) return ORPHEUS_ERR_INVALID_ARG;
        for (size_t j=1; j<n; ++j)
            if (((unsigned char)text[i+j] & 0xC0)!=0x80) return ORPHEUS_ERR_INVALID_ARG;
        unsigned char second=(unsigned char)text[i+1];
        if ((c==0xE0 && second<0xA0) || (c==0xED && second>=0xA0) ||
            (c==0xF0 && second<0x90) || (c==0xF4 && second>=0x90))
            return ORPHEUS_ERR_INVALID_ARG;
        i+=n;
    }
    return ORPHEUS_ERR_INVALID_ARG;
}
static int update_parameter(orpheus_builtin_noise_detector_ab_Parameters* p, const char* key, const OrpheusValue* value) {
    if (!p || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(key, "channels") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.i32<1 || value->value.i32>32) return ORPHEUS_ERR_INVALID_ARG;
        p->channels=value->value.i32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "fft_size") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.i32<4 || value->value.i32>4096) return ORPHEUS_ERR_INVALID_ARG;
        p->fft_size=value->value.i32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "batches") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.i32<1 || value->value.i32>64) return ORPHEUS_ERR_INVALID_ARG;
        p->batches=value->value.i32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "smoothing") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.i32<1 || value->value.i32>128) return ORPHEUS_ERR_INVALID_ARG;
        p->smoothing=value->value.i32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "threshold_db") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.f32<-1.200000000e+02f || value->value.f32>0.000000000e+00f) return ORPHEUS_ERR_INVALID_ARG;
        p->threshold_db=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "time_thres") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.f32<0.000000000e+00f || value->value.f32>1.000000000e+00f) return ORPHEUS_ERR_INVALID_ARG;
        p->time_thres=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "thd_n_db") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        p->thd_n_db=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "noise_frames") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        p->noise_frames=value->value.i32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "noise_ratio") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        p->noise_ratio=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "clicks") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        p->clicks=value->value.i32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "residue_pk") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        p->residue_pk=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "detail") == 0) {
        if (value->type!=ORPHEUS_VALUE_STRING) return ORPHEUS_ERR_INVALID_ARG;
        size_t length;
        if (valid_utf8_string(value->value.str, sizeof(p->detail), &length)!=ORPHEUS_OK)
            return ORPHEUS_ERR_INVALID_ARG;
        char text[sizeof(p->detail)]; memset(text, 0, sizeof(text));
        memcpy(text, value->value.str, length+1);
        memcpy(p->detail, text, sizeof(text));
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int normalize_parameters(orpheus_builtin_noise_detector_ab_Parameters* p, const char* key) {
    int result=orpheus_builtin_noise_detector_ab_normalize(p, key);
    if (result!=ORPHEUS_OK) return result;
    {
        OrpheusValue value={.type=ORPHEUS_VALUE_INT,.value.i32=p->channels};
        result=update_parameter(p, "channels", &value);
        if (result!=ORPHEUS_OK) return result;
    }
    {
        OrpheusValue value={.type=ORPHEUS_VALUE_INT,.value.i32=p->fft_size};
        result=update_parameter(p, "fft_size", &value);
        if (result!=ORPHEUS_OK) return result;
    }
    {
        OrpheusValue value={.type=ORPHEUS_VALUE_INT,.value.i32=p->batches};
        result=update_parameter(p, "batches", &value);
        if (result!=ORPHEUS_OK) return result;
    }
    {
        OrpheusValue value={.type=ORPHEUS_VALUE_INT,.value.i32=p->smoothing};
        result=update_parameter(p, "smoothing", &value);
        if (result!=ORPHEUS_OK) return result;
    }
    {
        OrpheusValue value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=p->threshold_db};
        result=update_parameter(p, "threshold_db", &value);
        if (result!=ORPHEUS_OK) return result;
    }
    {
        OrpheusValue value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=p->time_thres};
        result=update_parameter(p, "time_thres", &value);
        if (result!=ORPHEUS_OK) return result;
    }
    {
        OrpheusValue value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=p->thd_n_db};
        result=update_parameter(p, "thd_n_db", &value);
        if (result!=ORPHEUS_OK) return result;
    }
    {
        OrpheusValue value={.type=ORPHEUS_VALUE_INT,.value.i32=p->noise_frames};
        result=update_parameter(p, "noise_frames", &value);
        if (result!=ORPHEUS_OK) return result;
    }
    {
        OrpheusValue value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=p->noise_ratio};
        result=update_parameter(p, "noise_ratio", &value);
        if (result!=ORPHEUS_OK) return result;
    }
    {
        OrpheusValue value={.type=ORPHEUS_VALUE_INT,.value.i32=p->clicks};
        result=update_parameter(p, "clicks", &value);
        if (result!=ORPHEUS_OK) return result;
    }
    {
        OrpheusValue value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=p->residue_pk};
        result=update_parameter(p, "residue_pk", &value);
        if (result!=ORPHEUS_OK) return result;
    }
    {
        OrpheusValue value={.type=ORPHEUS_VALUE_STRING,.value.str=p->detail};
        result=update_parameter(p, "detail", &value);
        if (result!=ORPHEUS_OK) return result;
    }
    return ORPHEUS_OK;
}
static int parameter_value(const orpheus_builtin_noise_detector_ab_Parameters* p, const char* key, OrpheusValue* value) {
    (void)p; (void)key; (void)value;
    if (strcmp(key, "channels")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=p->channels; return ORPHEUS_OK;
    }
    if (strcmp(key, "fft_size")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=p->fft_size; return ORPHEUS_OK;
    }
    if (strcmp(key, "batches")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=p->batches; return ORPHEUS_OK;
    }
    if (strcmp(key, "smoothing")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=p->smoothing; return ORPHEUS_OK;
    }
    if (strcmp(key, "threshold_db")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=p->threshold_db; return ORPHEUS_OK;
    }
    if (strcmp(key, "time_thres")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=p->time_thres; return ORPHEUS_OK;
    }
    if (strcmp(key, "thd_n_db")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=p->thd_n_db; return ORPHEUS_OK;
    }
    if (strcmp(key, "noise_frames")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=p->noise_frames; return ORPHEUS_OK;
    }
    if (strcmp(key, "noise_ratio")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=p->noise_ratio; return ORPHEUS_OK;
    }
    if (strcmp(key, "clicks")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=p->clicks; return ORPHEUS_OK;
    }
    if (strcmp(key, "residue_pk")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=p->residue_pk; return ORPHEUS_OK;
    }
    if (strcmp(key, "detail")==0) {
        value->type=ORPHEUS_VALUE_STRING; value->value.str=p->detail; return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int set_parameter(void* state, const char* key, const OrpheusValue* value) {
    orpheus_builtin_noise_detector_ab_State* s=(orpheus_builtin_noise_detector_ab_State*)state;
    if (!s || !s->_orpheus_alive || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(key, "thd_n_db")==0) return ORPHEUS_ERR_UNSUPPORTED;
    if (strcmp(key, "noise_frames")==0) return ORPHEUS_ERR_UNSUPPORTED;
    if (strcmp(key, "noise_ratio")==0) return ORPHEUS_ERR_UNSUPPORTED;
    if (strcmp(key, "clicks")==0) return ORPHEUS_ERR_UNSUPPORTED;
    if (strcmp(key, "residue_pk")==0) return ORPHEUS_ERR_UNSUPPORTED;
    if (strcmp(key, "detail")==0) return ORPHEUS_ERR_UNSUPPORTED;
    orpheus_builtin_noise_detector_ab_Parameters before, candidate; snapshot_parameters(s, &before); candidate=before;
    int result=update_parameter(&candidate, key, value);
    if (result!=ORPHEUS_OK) return result;
    result=normalize_parameters(&candidate, key); if (result!=ORPHEUS_OK) return result;
    commit_parameters(s, &candidate); return ORPHEUS_OK;
}
static int get_parameter(void* state, const char* key, OrpheusValue* value) {
    orpheus_builtin_noise_detector_ab_State* s=(orpheus_builtin_noise_detector_ab_State*)state;
    if (!s || !s->_orpheus_alive || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_noise_detector_ab_Parameters before, checked; snapshot_parameters(s, &before); checked=before;
    OrpheusValue candidate={0};
    int result=orpheus_builtin_noise_detector_ab_get_parameter(s, key, &candidate);
    if (result==ORPHEUS_OK) result=update_parameter(&checked, key, &candidate);
    commit_parameters(s, &before);
    if (result!=ORPHEUS_ERR_NOT_FOUND) {
        if (result!=ORPHEUS_OK) return result;
        result=parameter_value(&checked, key, value);
        if (result!=ORPHEUS_OK) return result;
        if (strcmp(key, "detail")==0) {
            memcpy(s->_orpheus_readback_detail, checked.detail, sizeof(checked.detail));
            value->value.str=s->_orpheus_readback_detail;
        }
        return ORPHEUS_OK;
    }
    if (strcmp(key, "channels")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->channels; return ORPHEUS_OK;
    }
    if (strcmp(key, "fft_size")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->fft_size; return ORPHEUS_OK;
    }
    if (strcmp(key, "batches")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->batches; return ORPHEUS_OK;
    }
    if (strcmp(key, "smoothing")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->smoothing; return ORPHEUS_OK;
    }
    if (strcmp(key, "threshold_db")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->threshold_db; return ORPHEUS_OK;
    }
    if (strcmp(key, "time_thres")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->time_thres; return ORPHEUS_OK;
    }
    if (strcmp(key, "thd_n_db")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->thd_n_db; return ORPHEUS_OK;
    }
    if (strcmp(key, "noise_frames")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->noise_frames; return ORPHEUS_OK;
    }
    if (strcmp(key, "noise_ratio")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->noise_ratio; return ORPHEUS_OK;
    }
    if (strcmp(key, "clicks")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->clicks; return ORPHEUS_OK;
    }
    if (strcmp(key, "residue_pk")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->residue_pk; return ORPHEUS_OK;
    }
    if (strcmp(key, "detail")==0) {
        value->type=ORPHEUS_VALUE_STRING; value->value.str=s->detail; return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int configure_parameters(orpheus_builtin_noise_detector_ab_Parameters* p, const OrpheusConfig* config) {
    if (!config || (config->param_count && (!config->param_ids || !config->param_values)))
        return ORPHEUS_ERR_INVALID_ARG;
    default_parameters(p);
    bool explicit_channels=false;
    for (uint32_t i=0; i<config->param_count; ++i)
        if (config->param_ids[i] && strcmp(config->param_ids[i], "channels")==0) explicit_channels=true;
    if (!explicit_channels && config->channels) {
        if (config->channels>INT32_MAX) return ORPHEUS_ERR_INVALID_ARG;
        OrpheusValue channels={.type=ORPHEUS_VALUE_INT,.value.i32=(int32_t)config->channels};
        int result=update_parameter(p, "channels", &channels);
        if (result!=ORPHEUS_OK) return result;
    }
    for (uint32_t i=0; i<config->param_count; ++i) {
        int result=update_parameter(p, config->param_ids[i], &config->param_values[i]);
        if (result!=ORPHEUS_OK) return result;
    }
    int normalized=normalize_parameters(p, NULL); if (normalized!=ORPHEUS_OK) return normalized;
    return ORPHEUS_OK;
}
static int cleanup(orpheus_builtin_noise_detector_ab_State* s) {
    int result=ORPHEUS_OK;
    if (s->_orpheus_cleanup_pending) {
        s->_orpheus_cleanup_pending=false;
        result=orpheus_builtin_noise_detector_ab_destroy(s);
        memset(&s->algorithm, 0, sizeof(s->algorithm));
    }
    return result;
}
static int prepare(void* state, const OrpheusConfig* config) {
    orpheus_builtin_noise_detector_ab_State* s=(orpheus_builtin_noise_detector_ab_State*)state;
    if (!s || !s->_orpheus_alive) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_noise_detector_ab_Parameters candidate; int result=configure_parameters(&candidate, config);
    if (result!=ORPHEUS_OK) return result;
    result=cleanup(s); if (result!=ORPHEUS_OK) return result;
    commit_parameters(s, &candidate);
    s->_orpheus_cleanup_pending=true;
    result=orpheus_builtin_noise_detector_ab_prepare(s, config);
    if (result!=ORPHEUS_OK) { (void)cleanup(s); return result; }
    return ORPHEUS_OK;
}
static int create(void** state, const OrpheusConfig* config) {
    if (!state) return ORPHEUS_ERR_INVALID_ARG;
    *state=NULL;
    if (!config || !config->state_block) return ORPHEUS_ERR_INVALID_ARG;
    if ((uintptr_t)config->state_block % descriptor.alignment) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_noise_detector_ab_Parameters candidate; int result=configure_parameters(&candidate, config);
    if (result!=ORPHEUS_OK) return result;
    orpheus_builtin_noise_detector_ab_State* s=(orpheus_builtin_noise_detector_ab_State*)config->state_block;
    memset(s, 0, sizeof(*s)); commit_parameters(s, &candidate);
    s->_orpheus_alive=true; *state=s; return ORPHEUS_OK;
}
static int destroy(void* state) {
    orpheus_builtin_noise_detector_ab_State* s=(orpheus_builtin_noise_detector_ab_State*)state;
    if (!s) return ORPHEUS_ERR_INVALID_ARG;
    if (!s->_orpheus_alive) return ORPHEUS_OK;
    int result=cleanup(s); s->_orpheus_alive=false; return result;
}
static int reset(void* state) {
    orpheus_builtin_noise_detector_ab_State* s=(orpheus_builtin_noise_detector_ab_State*)state;
    if (!s || !s->_orpheus_alive) return ORPHEUS_ERR_INVALID_ARG;
    return orpheus_builtin_noise_detector_ab_reset(s);
}
static int register_slots(void* state, const OrpheusRegistry* reg) {
    orpheus_builtin_noise_detector_ab_State* s=(orpheus_builtin_noise_detector_ab_State*)state;
    if (!s || !s->_orpheus_alive || !reg || !reg->add) return ORPHEUS_ERR_INVALID_ARG;
    if (ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "通道数", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_AFFECTS_SIGNATURE, .min_i32=1, .max_i32=32)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, fft_size, ORPHEUS_SLOT_SETTING, "fft_size", "FFT窗口点数", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_AFFECTS_SIGNATURE, .min_i32=4, .max_i32=4096)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, batches, ORPHEUS_SLOT_SETTING, "batches", "顺序窗口数", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_AFFECTS_SIGNATURE, .min_i32=1, .max_i32=64)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, smoothing, ORPHEUS_SLOT_SETTING, "smoothing", "平滑窗口数", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT, .min_i32=1, .max_i32=128)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, threshold_db, ORPHEUS_SLOT_SETTING, "threshold_db", "失真噪声阈值", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="dB", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT, .min_f32=-1.200000000e+02f, .max_f32=0.000000000e+00f)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, time_thres, ORPHEUS_SLOT_SETTING, "time_thres", "时域残差阈值", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT, .min_f32=0.000000000e+00f, .max_f32=1.000000000e+00f)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, thd_n_db, ORPHEUS_SLOT_PROBE, "thd_n_db", "失真+噪声", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, noise_frames, ORPHEUS_SLOT_PROBE, "noise_frames", "噪声窗口数", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, noise_ratio, ORPHEUS_SLOT_PROBE, "noise_ratio", "噪声占比", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, clicks, ORPHEUS_SLOT_PROBE, "clicks", "突刺计数", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, residue_pk, ORPHEUS_SLOT_PROBE, "residue_pk", "残差峰值", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, detail, ORPHEUS_SLOT_PROBE, "detail", "频带明细", ORPHEUS_VALUE_STRING, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    return ORPHEUS_OK;
}
static int valid_buffer(const OrpheusBuffer* buffer, uint32_t channels,
        OrpheusSampleFormat format, size_t sample_size, size_t alignment) {
    return !buffer || (buffer->format == format && buffer->interleaved
        && buffer->channels == channels && channels > 0
        && buffer->frame_count <= buffer->frame_capacity
        && (!buffer->frame_capacity || buffer->data)
        && (!buffer->data || (uintptr_t)buffer->data % alignment == 0)
        && (size_t)buffer->frame_capacity <= SIZE_MAX / sample_size / channels);
}
static int process(void* state, const OrpheusProcessContext* ctx) {
    orpheus_builtin_noise_detector_ab_State* s=(orpheus_builtin_noise_detector_ab_State*)state;
    orpheus_builtin_noise_detector_ab_IO io={0};
    if (!s || !s->_orpheus_alive || !ctx) return ORPHEUS_ERR_INVALID_ARG;
    if (s->fft_size < 4 || s->fft_size > 4096) return ORPHEUS_ERR_INVALID_ARG;
    if (s->batches < 1 || s->batches > 64) return ORPHEUS_ERR_INVALID_ARG;
    double _orpheus_payload_ref=(double)(((double)(s->fft_size) * (double)(s->batches)));
    if (!isfinite(_orpheus_payload_ref) || _orpheus_payload_ref < 1
            || _orpheus_payload_ref > UINT32_MAX || _orpheus_payload_ref != (uint32_t)_orpheus_payload_ref)
        return ORPHEUS_ERR_INVALID_ARG;
    io.ref.input_index=0;
    io.ref.buffer=(ctx->inputs && ctx->input_count > (0)) ? ctx->inputs[0] : NULL;
    if (!valid_buffer(io.ref.buffer, (uint32_t)(s->channels),
        ORPHEUS_FORMAT_F32, sizeof(float), _Alignof(float)))
        return ORPHEUS_ERR_INVALID_ARG;
    io.ref.samples=io.ref.buffer ? (const float*)io.ref.buffer->data : NULL;
    if (io.ref.buffer && io.ref.buffer->frame_count != (uint32_t)_orpheus_payload_ref)
        return ORPHEUS_ERR_INVALID_ARG;
    if (s->fft_size < 4 || s->fft_size > 4096) return ORPHEUS_ERR_INVALID_ARG;
    if (s->batches < 1 || s->batches > 64) return ORPHEUS_ERR_INVALID_ARG;
    double _orpheus_payload_in=(double)(((double)(s->fft_size) * (double)(s->batches)));
    if (!isfinite(_orpheus_payload_in) || _orpheus_payload_in < 1
            || _orpheus_payload_in > UINT32_MAX || _orpheus_payload_in != (uint32_t)_orpheus_payload_in)
        return ORPHEUS_ERR_INVALID_ARG;
    io.in.input_index=1;
    io.in.buffer=(ctx->inputs && ctx->input_count > (1)) ? ctx->inputs[1] : NULL;
    if (!valid_buffer(io.in.buffer, (uint32_t)(s->channels),
        ORPHEUS_FORMAT_F32, sizeof(float), _Alignof(float)))
        return ORPHEUS_ERR_INVALID_ARG;
    io.in.samples=io.in.buffer ? (const float*)io.in.buffer->data : NULL;
    if (io.in.buffer && io.in.buffer->frame_count != (uint32_t)_orpheus_payload_in)
        return ORPHEUS_ERR_INVALID_ARG;
    if (s->fft_size < 4 || s->fft_size > 4096) return ORPHEUS_ERR_INVALID_ARG;
    if (s->batches < 1 || s->batches > 64) return ORPHEUS_ERR_INVALID_ARG;
    double _orpheus_payload_out=(double)(((double)(s->fft_size) * (double)(s->batches)));
    if (!isfinite(_orpheus_payload_out) || _orpheus_payload_out < 1
            || _orpheus_payload_out > UINT32_MAX || _orpheus_payload_out != (uint32_t)_orpheus_payload_out)
        return ORPHEUS_ERR_INVALID_ARG;
    io.out.active=true;
    io.out.drive_port_no=1u;
    io.out.buffer=(ctx->outputs && ctx->output_count > (0)) ? ctx->outputs[0] : NULL;
    if (!valid_buffer(io.out.buffer, (uint32_t)(s->channels),
        ORPHEUS_FORMAT_F32, sizeof(float), _Alignof(float)))
        return ORPHEUS_ERR_INVALID_ARG;
    io.out.samples=io.out.buffer ? (float*)io.out.buffer->data : NULL;
    if (io.out.buffer) {
        if ((uint32_t)_orpheus_payload_out > io.out.buffer->frame_capacity) return ORPHEUS_ERR_INVALID_ARG;
        io.out.buffer->frame_count=(uint32_t)_orpheus_payload_out;
    }
    io.out.delivery=(OrpheusDelivery){.buffer=io.out.buffer, .frame_index=ctx->frame_index,
        .epoch=ctx->epoch, .timeline_flags=ctx->timeline_flags};
    return orpheus_builtin_noise_detector_ab_algorithm(s, &io, ctx);
}
static const OrpheusComponentInterface interface = {
    .get_descriptor=get_descriptor, .create=create, .destroy=destroy,
    .prepare=prepare, .reset=reset, .process=process,
    .set_parameter=set_parameter, .get_parameter=get_parameter, .register_slots=register_slots,
    .hook=NULL
};
#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) { return &interface; }
