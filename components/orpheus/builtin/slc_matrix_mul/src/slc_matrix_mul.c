#include "orpheus_slc_matrix_mul.h"

#include <math.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

/* ---- 辅助函数 ---- */

static float read_float(const OrpheusConfig* config, const char* id, float fallback) {
    for (uint32_t i = 0; i < config->param_count; ++i) {
        if (config->param_ids[i] && strcmp(config->param_ids[i], id) == 0) {
            if (config->param_values[i].type == ORPHEUS_VALUE_FLOAT) return config->param_values[i].value.f32;
            if (config->param_values[i].type == ORPHEUS_VALUE_INT)   return (float)config->param_values[i].value.i32;
        }
    }
    return fallback;
}

static int32_t read_int(const OrpheusConfig* config, const char* id, int32_t fallback) {
    for (uint32_t i = 0; i < config->param_count; ++i) {
        if (config->param_ids[i] && strcmp(config->param_ids[i], id) == 0) {
            if (config->param_values[i].type == ORPHEUS_VALUE_INT)   return config->param_values[i].value.i32;
            if (config->param_values[i].type == ORPHEUS_VALUE_FLOAT) return (int32_t)config->param_values[i].value.f32;
        }
    }
    return fallback;
}

static const char* read_string(const OrpheusConfig* config, const char* id, const char* fallback) {
    for (uint32_t i = 0; i < config->param_count; ++i) {
        if (config->param_ids[i] && strcmp(config->param_ids[i], id) == 0 &&
            config->param_values[i].type == ORPHEUS_VALUE_STRING) {
            return config->param_values[i].value.str ? config->param_values[i].value.str : fallback;
        }
    }
    return fallback;
}

/* 解析逗号分隔的浮点数组 */
static uint32_t parse_floats(const char* text, float* out, uint32_t max_count) {
    if (text == NULL) return 0;
    uint32_t count = 0;
    const char* p = text;
    while (*p && count < max_count) {
        out[count++] = (float)strtod(p, (char**)&p);
        while (*p == ',' || *p == ' ' || *p == '\t' || *p == '\n') ++p;
    }
    return count;
}

/* 计算插值后的目标矩阵 */
static void compute_target(SlcMatrixMulState* s) {
    uint32_t elem = s->rows * s->cols;
    if (elem == 0 || elem > SLC_MM_MAX_ELEM) return;

    if (s->numTables <= 1) {
        memcpy(s->target, s->tables, elem * sizeof(float));
        return;
    }

    /* 查找 interpIndex 所在的分段 [i, i+1) */
    float idx = s->interpIndex;
    uint32_t i = 0;
    if (idx <= s->interpX[0]) {
        memcpy(s->target, s->tables, elem * sizeof(float));
        return;
    }
    if (idx >= s->interpX[s->numTables - 1]) {
        memcpy(s->target, s->tables + (s->numTables - 1) * elem, elem * sizeof(float));
        return;
    }
    for (i = 0; i < s->numTables - 1; ++i) {
        if (idx >= s->interpX[i] && idx < s->interpX[i + 1]) break;
    }
    if (i >= s->numTables - 1) i = s->numTables - 2;

    /* 计算分段内分数位置 t */
    float x0 = s->interpX[i];
    float x1 = s->interpX[i + 1];
    float span = x1 - x0;
    float t = (span > 1e-12f) ? (idx - x0) / span : 0.0f;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    const float* ta = s->tables + i * elem;
    const float* tb = s->tables + (i + 1) * elem;

    if (s->interpMethod == 1) {
        /* 对数线性：target = exp((1-t)*ln(a) + t*ln(b)) */
        for (uint32_t k = 0; k < elem; ++k) {
            float a = fmaxf(ta[k], SLC_MM_SILENT_GAIN);
            float b = fmaxf(tb[k], SLC_MM_SILENT_GAIN);
            s->target[k] = expf((1.0f - t) * logf(a) + t * logf(b));
        }
    } else {
        /* 线性：target = (1-t)*a + t*b */
        for (uint32_t k = 0; k < elem; ++k) {
            s->target[k] = (1.0f - t) * ta[k] + t * tb[k];
        }
    }
}

/* ---- 参数描述符 ---- */

static const OrpheusParameter smm_params[] = {
    { .id = "rows", .name = "\xe8\xbe\x93\xe5\x87\xba\xe8\xa1\x8c\xe6\x95\xb0",
      .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 2 },
      .min_i32 = 1, .max_i32 = 32, .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = true },
    { .id = "cols", .name = "\xe8\xbe\x93\xe5\x85\xa5\xe5\x88\x97\xe6\x95\xb0",
      .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 2 },
      .min_i32 = 1, .max_i32 = 32, .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = true },
    { .id = "num_tables", .name = "\xe6\x8f\x92\xe5\x80\xbc\xe8\xa1\xa8\xe6\x95\xb0\xe9\x87\x8f",
      .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 1 },
      .min_i32 = 1, .max_i32 = SLC_MM_MAX_TABLES, .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true },
    { .id = "tables", .name = "\xe5\xa2\x9e\xe7\x9b\x8a\xe8\xa1\xa8",
      .type = ORPHEUS_VALUE_STRING,
      .default_value = { .type = ORPHEUS_VALUE_STRING, .value.str = "1, 0, 0, 1" },
      .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true },
    { .id = "interp_x", .name = "\xe6\x8f\x92\xe5\x80\xbc\xe6\x96\xad\xe7\x82\xb9",
      .type = ORPHEUS_VALUE_STRING,
      .default_value = { .type = ORPHEUS_VALUE_STRING, .value.str = "0" },
      .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true },
    { .id = "interp_index", .name = "\xe6\x8f\x92\xe5\x80\xbc\xe4\xbd\x8d\xe7\xbd\xae",
      .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 0.0f },
      .update_policy = ORPHEUS_UPDATE_IMMEDIATE,
      .readback = true, .persistent = true },
    { .id = "interp_method", .name = "\xe6\x8f\x92\xe5\x80\xbc\xe6\x96\xb9\xe6\xb3\x95",
      .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 0 },
      .min_i32 = 0, .max_i32 = 1, .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true },
    { .id = "ramp_coeff", .name = "IIR\xe6\x96\x9c\xe5\x9d\xa1\xe7\xb3\xbb\xe6\x95\xb0",
      .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 0.0f },
      .min_f32 = 0.0f, .max_f32 = 1.0f, .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true },
    { .id = "freeze", .name = "\xe5\x86\xbb\xe7\xbb\x93",
      .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 0 },
      .min_i32 = 0, .max_i32 = 1, .update_policy = ORPHEUS_UPDATE_IMMEDIATE,
      .readback = true, .persistent = true },
    {.id="execution_mode",.name="执行模式（0插值请求，1序列接收目标）",.type=ORPHEUS_VALUE_INT,
     .default_value={.type=ORPHEUS_VALUE_INT,.value.i32=0},.min_i32=0,.max_i32=1,
     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.readback=true,.persistent=true},
    {.id="requested_gains",.name="待接收目标矩阵",.type=ORPHEUS_VALUE_STRING,
     .default_value={.type=ORPHEUS_VALUE_STRING,.value.str=""},
     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.readback=true,.persistent=true},
    {.id="sequence",.name="目标请求序列（奇数冻结）",.type=ORPHEUS_VALUE_INT,
     .default_value={.type=ORPHEUS_VALUE_INT,.value.i32=0},.min_i32=INT32_MIN,.max_i32=INT32_MAX,
     .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY,.readback=true,.persistent=true}
};

static const OrpheusPort smm_ports[] = {
    { .id = "in", .direction = ORPHEUS_PORT_INPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "cols" },
    { .id = "out", .direction = ORPHEUS_PORT_OUTPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "rows" }
};

static const OrpheusComponentDescriptor smm_descriptor = {
    .id = "orpheus.builtin.slc_matrix_mul",
    .version = "1.1.0", .abi_version = ORPHEUS_ABI_VERSION,
    .ports = smm_ports, .port_count = 2,
    .params = smm_params, .param_count = sizeof(smm_params)/sizeof(smm_params[0]),
    .state_size = sizeof(SlcMatrixMulState), .scratch_size = 0, .alignment = 8,
    .latency_samples = 0, .realtime_safe = true, .supports_inplace = false
};

static const OrpheusComponentDescriptor* smm_get_descriptor(void) { return &smm_descriptor; }

/* ---- 生命周期 ---- */

static int smm_create(void** state, const OrpheusConfig* config) {
    if (config != NULL && config->state_block != NULL) {
        *state = config->state_block;
        return ORPHEUS_OK;
    }
    *state = calloc(1, sizeof(SlcMatrixMulState));
    return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}

static int smm_destroy(void* state) { (void)state; return ORPHEUS_OK; }

static int prepare_sequence(SlcMatrixMulState* s,const OrpheusConfig* config) {
    for (unsigned i=0;i<config->param_count;++i) {
        for (unsigned j=0;j<sizeof(smm_params)/sizeof(smm_params[0]);++j) {
            if (!config->param_ids[i] || strcmp(config->param_ids[i],smm_params[j].id)!=0) continue;
            const OrpheusValue* value=&config->param_values[i];
            if (value->type!=smm_params[j].type || (value->type==ORPHEUS_VALUE_STRING && !value->value.str))
                return ORPHEUS_ERR_INVALID_ARG;
        }
    }
    int32_t rows=read_int(config,"rows",2),cols=read_int(config,"cols",2);
    if (rows<1 || rows>32 || cols<1 || cols>32) return ORPHEUS_ERR_INVALID_ARG;
    s->rows=(unsigned)rows; s->cols=(unsigned)cols;
    s->rampCoeff=read_float(config,"ramp_coeff",0);
    if (!isfinite(s->rampCoeff) || s->rampCoeff<0 || s->rampCoeff>1) return ORPHEUS_ERR_INVALID_ARG;
    s->sequence=read_int(config,"sequence",0); s->last_sequence=0; s->initialized=false;
    const unsigned count=s->rows*s->cols;
    memset(s->active,0,count*sizeof(float)); memset(s->target,0,count*sizeof(float));
    memset(s->requested_gains,0,count*sizeof(float));
    const char* text=read_string(config,"requested_gains","");
    if (!*text) return ORPHEUS_OK;
    for (unsigned i=0;i<count;++i) {
        char* end;
        float value=strtof(text,&end);
        if (end==text || !isfinite(value)) return ORPHEUS_ERR_INVALID_ARG;
        while (*end==' ' || *end=='\t' || *end=='\r' || *end=='\n') ++end;
        if (i+1<count ? *end!=',' : *end!='\0') return ORPHEUS_ERR_INVALID_ARG;
        s->requested_gains[i]=value;
        text=i+1<count ? end+1 : end;
    }
    return ORPHEUS_OK;
}

static int smm_prepare(void* state, const OrpheusConfig* config) {
    if (!state || !config || (config->param_count && (!config->param_ids || !config->param_values)))
        return ORPHEUS_ERR_INVALID_ARG;
    SlcMatrixMulState* s = (SlcMatrixMulState*)state;
    s->execution_mode=0;
    for (unsigned i=0;i<config->param_count;++i) if (config->param_ids[i] && strcmp(config->param_ids[i],"execution_mode")==0) {
        if (config->param_values[i].type!=ORPHEUS_VALUE_INT || config->param_values[i].value.i32<0 ||
            config->param_values[i].value.i32>1) return ORPHEUS_ERR_INVALID_ARG;
        s->execution_mode=config->param_values[i].value.i32;
    }
    if (s->execution_mode==1) return prepare_sequence(s,config);
    s->rows = (uint32_t)read_int(config, "rows", 2);
    s->cols = (uint32_t)read_int(config, "cols", 2);
    s->numTables = (uint32_t)read_int(config, "num_tables", 1);
    s->interpMethod = (uint32_t)read_int(config, "interp_method", 0);
    s->rampCoeff = read_float(config, "ramp_coeff", 0.0f);
    s->interpIndex = read_float(config, "interp_index", 0.0f);
    s->freeze = read_int(config, "freeze", 0);

    if (s->rows < 1) s->rows = 1;
    if (s->rows > 32) s->rows = 32;
    if (s->cols < 1) s->cols = 1;
    if (s->cols > 32) s->cols = 32;
    if (s->numTables < 1) s->numTables = 1;
    if (s->numTables > SLC_MM_MAX_TABLES) s->numTables = SLC_MM_MAX_TABLES;

    uint32_t elem = s->rows * s->cols;
    uint32_t total = s->numTables * elem;
    if (total > SLC_MM_MAX_TABLES * SLC_MM_MAX_ELEM) total = SLC_MM_MAX_TABLES * SLC_MM_MAX_ELEM;

    /* 解析增益表 */
    memset(s->tables, 0, sizeof(s->tables));
    uint32_t parsed = parse_floats(read_string(config, "tables", "1, 0, 0, 1"),
                                    s->tables, total);
    /* 若未提供足量数据，填充单位矩阵到第一张表 */
    if (parsed < elem) {
        for (uint32_t i = 0; i < s->rows && i < s->cols; ++i)
            s->tables[i * s->cols + i] = 1.0f;
    }

    /* 解析插值断点 */
    memset(s->interpX, 0, sizeof(s->interpX));
    uint32_t nx = parse_floats(read_string(config, "interp_x", "0"),
                                s->interpX, s->numTables);
    if (nx < s->numTables) {
        /* 默认均分 0..1 */
        for (uint32_t i = 0; i < s->numTables; ++i)
            s->interpX[i] = (s->numTables > 1)
                ? (float)i / (float)(s->numTables - 1) : 0.0f;
    }

    /* 初始化 active = target */
    compute_target(s);
    memcpy(s->active, s->target, elem * sizeof(float));
    s->targetDirty = 0;

    return ORPHEUS_OK;
}

static int smm_reset(void* state) {
    SlcMatrixMulState* s = (SlcMatrixMulState*)state;
    if (s->execution_mode==1) {
        s->initialized=false; s->last_sequence=0;
        return ORPHEUS_OK;
    }
    uint32_t elem = s->rows * s->cols;
    compute_target(s);
    memcpy(s->active, s->target, elem * sizeof(float));
    s->targetDirty = 0;
    return ORPHEUS_OK;
}

/* ---- 实时处理 ---- */

static int process_sequence(SlcMatrixMulState* s,const OrpheusProcessContext* ctx) {
    if (!ctx || ctx->input_count<1 || ctx->output_count<1 || !ctx->inputs || !ctx->outputs ||
        !ctx->inputs[0] || !ctx->outputs[0]) return ORPHEUS_ERR_INVALID_ARG;
    const OrpheusBuffer* input=ctx->inputs[0]; OrpheusBuffer* output=ctx->outputs[0];
    const unsigned rows=s->rows,cols=s->cols,frames=ctx->frame_count,count=rows*cols;
    if (rows<1 || rows>32 || cols<1 || cols>32 || input->channels!=cols || output->channels!=rows ||
        input->format!=ORPHEUS_FORMAT_F32 || output->format!=ORPHEUS_FORMAT_F32 ||
        input->frame_capacity<frames || output->frame_capacity<frames ||
        (frames && (!input->data || !output->data)) || !isfinite(s->rampCoeff) || s->rampCoeff<0 || s->rampCoeff>1)
        return ORPHEUS_ERR_INVALID_ARG;
    for (unsigned i=0;i<count;++i)
        if (!isfinite(s->requested_gains[i]) || (s->initialized && (!isfinite(s->active[i]) || !isfinite(s->target[i]))))
            return ORPHEUS_ERR_INVALID_ARG;
    if (!frames) { output->frame_count=0; return ORPHEUS_OK; }
    if (!s->initialized) {
        memcpy(s->active,s->requested_gains,count*sizeof(float));
        memcpy(s->target,s->requested_gains,count*sizeof(float));
        s->activeCoeff=s->rampCoeff; s->initialized=true;
    }
    const bool frozen=((uint32_t)s->sequence & 1u)!=0;
    if (s->last_sequence!=s->sequence) {
        if (!frozen) for (unsigned i=0;i<count;++i) {
            s->active[i]=fmaxf(s->active[i],SLC_MM_SILENT_GAIN);
            s->target[i]=s->requested_gains[i];
        }
        s->last_sequence=s->sequence;
    }
    const float* x=(const float*)input->data; float* y=(float*)output->data;
    const float coefficient=s->activeCoeff,other=1-coefficient;
    for (unsigned n=0;n<frames;++n) {
        if (!frozen) for (unsigned i=0;i<count;++i)
            s->active[i]=other*s->target[i]+coefficient*s->active[i];
        for (unsigned r=0;r<rows;++r) {
            float value=0;
            for (unsigned c=0;c<cols;++c) value+=x[n*cols+c]*s->active[r*cols+c];
            y[n*rows+r]=value;
        }
    }
    output->frame_count=frames; return ORPHEUS_OK;
}

static int smm_process(void* state, const OrpheusProcessContext* ctx) {
    SlcMatrixMulState* s = (SlcMatrixMulState*)state;
    if (s->execution_mode==1) return process_sequence(s,ctx);
    const OrpheusBuffer* in = ctx->inputs[0];
    OrpheusBuffer* out = ctx->outputs[0];
    if (in == NULL || out == NULL) return ORPHEUS_ERR_INVALID_ARG;

    uint32_t frames = ctx->frame_count;
    uint32_t rows = s->rows;
    uint32_t cols = s->cols;
    uint32_t elem = rows * cols;

    /* 若插值位置变化，重算目标矩阵 */
    if (s->targetDirty) {
        compute_target(s);
        s->targetDirty = 0;
    }

    const float* in_data = (const float*)in->data;
    float* out_data = (float*)out->data;
    float c = s->rampCoeff;
    float oneMinusC = 1.0f - c;
    int32_t frozen = s->freeze;

    for (uint32_t f = 0; f < frames; ++f) {
        /* 逐样本 IIR 渐变 */
        if (!frozen) {
            if (c <= 1e-6f) {
                /* ramp_coeff ~= 0: 直接 snap 到 target */
                memcpy(s->active, s->target, elem * sizeof(float));
            } else {
                for (uint32_t k = 0; k < elem; ++k)
                    s->active[k] = c * s->active[k] + oneMinusC * s->target[k];
            }
        }

        /* 矩阵乘: out[r] = sum(active[r*cols+c] * in[c]) */
        const float* x = in_data + f * cols;
        float* y = out_data + f * rows;
        for (uint32_t r = 0; r < rows; ++r) {
            float acc = 0.0f;
            const float* row = s->active + r * cols;
            for (uint32_t col = 0; col < cols; ++col)
                acc += row[col] * x[col];
            y[r] = acc;
        }
    }

    out->frame_count = frames;
    return ORPHEUS_OK;
}

/* ---- 参数读写 ---- */

static int smm_set_parameter(void* state, const char* param_id, const OrpheusValue* value) {
    SlcMatrixMulState* s = (SlcMatrixMulState*)state;
    if (s->execution_mode==1) {
        if (strcmp(param_id,"sequence")!=0 || value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        s->sequence=value->value.i32; return ORPHEUS_OK;
    }
    if (strcmp(param_id, "interp_index") == 0) {
        if (value->type == ORPHEUS_VALUE_FLOAT)
            s->interpIndex = value->value.f32;
        else if (value->type == ORPHEUS_VALUE_INT)
            s->interpIndex = (float)value->value.i32;
        else
            return ORPHEUS_ERR_INVALID_ARG;
        s->targetDirty = 1;
        return ORPHEUS_OK;
    }
    if (strcmp(param_id, "freeze") == 0) {
        if (value->type == ORPHEUS_VALUE_INT)
            s->freeze = value->value.i32;
        else if (value->type == ORPHEUS_VALUE_FLOAT)
            s->freeze = (int32_t)value->value.f32;
        else
            return ORPHEUS_ERR_INVALID_ARG;
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}

static int smm_get_parameter(void* state, const char* param_id, OrpheusValue* value) {
    SlcMatrixMulState* s = (SlcMatrixMulState*)state;
    if (strcmp(param_id,"sequence")==0 || strcmp(param_id,"last_sequence")==0) {
        value->type=ORPHEUS_VALUE_INT;
        value->value.i32=strcmp(param_id,"sequence")==0 ? s->sequence : s->last_sequence;
        return ORPHEUS_OK;
    }
    if (strcmp(param_id,"initialized")==0) {
        value->type=ORPHEUS_VALUE_BOOL; value->value.b=s->initialized; return ORPHEUS_OK;
    }
    if (strcmp(param_id, "rows") == 0) {
        value->type = ORPHEUS_VALUE_INT; value->value.i32 = (int32_t)s->rows; return ORPHEUS_OK;
    }
    if (strcmp(param_id, "cols") == 0) {
        value->type = ORPHEUS_VALUE_INT; value->value.i32 = (int32_t)s->cols; return ORPHEUS_OK;
    }
    if (strcmp(param_id, "num_tables") == 0) {
        value->type = ORPHEUS_VALUE_INT; value->value.i32 = (int32_t)s->numTables; return ORPHEUS_OK;
    }
    if (strcmp(param_id, "interp_index") == 0) {
        value->type = ORPHEUS_VALUE_FLOAT; value->value.f32 = s->interpIndex; return ORPHEUS_OK;
    }
    if (strcmp(param_id, "freeze") == 0) {
        value->type = ORPHEUS_VALUE_INT; value->value.i32 = s->freeze; return ORPHEUS_OK;
    }
    if (strcmp(param_id, "ramp_coeff") == 0) {
        value->type = ORPHEUS_VALUE_FLOAT; value->value.f32 = s->rampCoeff; return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}

/* ---- 槽注册 ---- */

static int smm_register_slots(void* state, const OrpheusRegistry* reg) {
    SlcMatrixMulState* s = (SlcMatrixMulState*)state;
    ORPHEUS_REG_ARRAY(reg,s,requested_gains,s->rows*s->cols,ORPHEUS_SLOT_BULK,
                     "requested_gains","待接收目标矩阵",ORPHEUS_VALUE_FLOAT,
                     .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY,.flags=ORPHEUS_SLOT_DOUBLE_BUFFERED);
    ORPHEUS_REG_ARRAY(reg,s,active,s->rows*s->cols,ORPHEUS_SLOT_BULK,"active_gains","实际当前矩阵",ORPHEUS_VALUE_FLOAT,
                     .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY,.flags=ORPHEUS_SLOT_DOUBLE_BUFFERED);
    ORPHEUS_REG_ARRAY(reg,s,target,s->rows*s->cols,ORPHEUS_SLOT_BULK,"accepted_targets","已接收目标矩阵",ORPHEUS_VALUE_FLOAT,
                     .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY,.flags=ORPHEUS_SLOT_DOUBLE_BUFFERED);
    ORPHEUS_REG_SLOT(reg,s,sequence,ORPHEUS_SLOT_SETTING,"sequence","目标请求序列（奇数冻结）",ORPHEUS_VALUE_INT,
                    .min_i32=INT32_MIN,.max_i32=INT32_MAX,.update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY,
                    .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg,s,last_sequence,ORPHEUS_SLOT_PROBE,"last_sequence","已处理序列",ORPHEUS_VALUE_INT,
                    .min_i32=INT32_MIN,.max_i32=INT32_MAX);
    ORPHEUS_REG_SLOT(reg,s,initialized,ORPHEUS_SLOT_PROBE,"initialized","包络已初始化",ORPHEUS_VALUE_BOOL);
    ORPHEUS_REG_SLOT(reg, s, rows, ORPHEUS_SLOT_SETTING, "rows",
                     "\xe8\xbe\x93\xe5\x87\xba\xe8\xa1\x8c\xe6\x95\xb0",
                     ORPHEUS_VALUE_INT, .min_i32=1, .max_i32=32,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    ORPHEUS_REG_SLOT(reg, s, cols, ORPHEUS_SLOT_SETTING, "cols",
                     "\xe8\xbe\x93\xe5\x85\xa5\xe5\x88\x97\xe6\x95\xb0",
                     ORPHEUS_VALUE_INT, .min_i32=1, .max_i32=32,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    ORPHEUS_REG_SLOT(reg, s, numTables, ORPHEUS_SLOT_SETTING, "num_tables",
                     "\xe6\x8f\x92\xe5\x80\xbc\xe8\xa1\xa8\xe6\x95\xb0\xe9\x87\x8f",
                     ORPHEUS_VALUE_INT, .min_i32=1, .max_i32=SLC_MM_MAX_TABLES,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg, s, interpIndex, ORPHEUS_SLOT_SETTING, "interp_index",
                     "\xe6\x8f\x92\xe5\x80\xbc\xe4\xbd\x8d\xe7\xbd\xae",
                     ORPHEUS_VALUE_FLOAT,
                     .update_policy=ORPHEUS_UPDATE_IMMEDIATE,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK |
                            (s->execution_mode==0 ? ORPHEUS_SLOT_DIRECT_WRITE : 0));
    ORPHEUS_REG_SLOT(reg, s, freeze, ORPHEUS_SLOT_SETTING, "freeze",
                     "\xe5\x86\xbb\xe7\xbb\x93",
                     ORPHEUS_VALUE_INT, .min_i32=0, .max_i32=1,
                     .update_policy=ORPHEUS_UPDATE_IMMEDIATE,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK |
                            (s->execution_mode==0 ? ORPHEUS_SLOT_DIRECT_WRITE : 0));
    ORPHEUS_REG_SLOT(reg, s, rampCoeff, ORPHEUS_SLOT_SETTING, "ramp_coeff",
                     "IIR\xe6\x96\x9c\xe5\x9d\xa1\xe7\xb3\xbb\xe6\x95\xb0",
                     ORPHEUS_VALUE_FLOAT, .min_f32=0.0f, .max_f32=1.0f,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
    return ORPHEUS_OK;
}

/* ---- 接口 ---- */

static const OrpheusComponentInterface smm_interface = {
    .get_descriptor = smm_get_descriptor, .create = smm_create, .destroy = smm_destroy,
    .prepare = smm_prepare, .reset = smm_reset, .process = smm_process,
    .set_parameter = smm_set_parameter, .get_parameter = smm_get_parameter,
    .get_state_value = NULL, .register_slots = smm_register_slots
};

#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) {
    return &smm_interface;
}