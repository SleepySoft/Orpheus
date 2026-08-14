#include "orpheus_noise_detector.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define PI_F 3.14159265358979f
static uint32_t is_pow2(uint32_t v){ return v && (v&(v-1))==0; }

static int ndint(const OrpheusConfig* c,const char* id,int fb){
    for(uint32_t i=0;i<c->param_count;++i) if(c->param_ids[i]&&!strcmp(c->param_ids[i],id)){
        if(c->param_values[i].type==ORPHEUS_VALUE_INT) return c->param_values[i].value.i32;
        if(c->param_values[i].type==ORPHEUS_VALUE_FLOAT) return (int)c->param_values[i].value.f32;}
    return fb;
}
static float ndflt(const OrpheusConfig* c,const char* id,float fb){
    for(uint32_t i=0;i<c->param_count;++i) if(c->param_ids[i]&&!strcmp(c->param_ids[i],id)){
        if(c->param_values[i].type==ORPHEUS_VALUE_FLOAT) return c->param_values[i].value.f32;
        if(c->param_values[i].type==ORPHEUS_VALUE_INT) return (float)c->param_values[i].value.i32;}
    return fb;
}
static void ndfft(float* re,float* im,uint32_t n){
    for(uint32_t i=1,j=0;i<n;++i){uint32_t b=n>>1;for(;j&b;b>>=1)j^=b;j^=b;if(i<j){float t=re[i];re[i]=re[j];re[j]=t;t=im[i];im[i]=im[j];im[j]=t;}}
    for(uint32_t len=2;len<=n;len<<=1){float ang=-2.0f*PI_F/(float)len,cw=cosf(ang),sw=sinf(ang);
        for(uint32_t i=0;i<n;i+=len){float cwr=1,cwi=0;
            for(uint32_t k=0;k<len/2;++k){uint32_t a=i+k,b=i+k+len/2;
                float tr=cwr*re[b]-cwi*im[b],ti=cwr*im[b]+cwi*re[b];
                re[b]=re[a]-tr;im[b]=im[a]-ti;re[a]+=tr;im[a]+=ti;
                float nw=cwr*cw-cwi*sw;cwi=cwr*sw+cwi*cw;cwr=nw;}}}
}
static void nd_free(NoiseDetectorState* s){ free(s->win);free(s->rea);free(s->ima); s->win=s->rea=s->ima=NULL; }
static int nd_create(void** state,const OrpheusConfig* c){ if(c&&c->state_block){*state=c->state_block;return ORPHEUS_OK;} *state=calloc(1,sizeof(NoiseDetectorState)); return *state?ORPHEUS_OK:ORPHEUS_ERR_OUT_OF_MEMORY; }
static int nd_destroy(void* state){ nd_free((NoiseDetectorState*)state); return ORPHEUS_OK; }
static int nd_prepare(void* state,const OrpheusConfig* c){
    NoiseDetectorState* s=(NoiseDetectorState*)state; nd_free(s); memset(s,0,sizeof(*s));
    s->channels = c->channels>0 ? (c->channels<ND_MAX_CH?c->channels:ND_MAX_CH):2;
    s->fft_size=(uint32_t)ndint(c,"fft_size",0);
    if(s->fft_size==0||!is_pow2(s->fft_size)){uint32_t b=c->block_size?c->block_size:128;s->fft_size=b;while(!is_pow2(s->fft_size)&&s->fft_size>2)s->fft_size>>=1;if(s->fft_size<2)s->fft_size=2;}
    if(s->fft_size>ND_MAX_FFT)s->fft_size=ND_MAX_FFT; s->half=s->fft_size/2; if(s->half<1)s->half=1;
    s->clip_level=ndflt(c,"clip_level",0.999f);
    s->click_thres=ndflt(c,"click_thres",0.3f);
    s->win=(float*)malloc(s->fft_size*sizeof(float));
    s->rea=(float*)malloc(s->fft_size*sizeof(float));
    s->ima=(float*)malloc(s->fft_size*sizeof(float));
    if(!s->win||!s->rea||!s->ima){ nd_free(s); return ORPHEUS_ERR_OUT_OF_MEMORY; }
    for(uint32_t k=0;k<s->fft_size;++k) s->win[k]=0.5f-0.5f*cosf(2.0f*PI_F*(float)k/(float)(s->fft_size-1));
    return ORPHEUS_OK;
}
static int nd_reset(void* state){ NoiseDetectorState* s=(NoiseDetectorState*)state; s->flatness=0;s->flatness_ema=0;s->noise_floor_db=0;s->clicks=0;s->clip_ratio=0;s->total_samples=0;s->clipped_samples=0; return ORPHEUS_OK; }

static float block_flatness(const float* in,uint32_t frames,uint32_t ch,uint32_t cidx,uint32_t fft,const float* win,float* rea,float* ima){
    if(frames<fft) return 1.0f;
    uint32_t nw=frames/fft; double sp=0,slp=0; uint32_t bins=0;
    for(uint32_t w=0;w<nw;++w){size_t off=(size_t)w*fft;
        for(uint32_t k=0;k<fft;++k){rea[k]=in[(off+k)*ch+cidx]*win[k];ima[k]=0.0f;}
        ndfft(rea,ima,fft);
        for(uint32_t b=1;b<fft/2;++b){float p=rea[b]*rea[b]+ima[b]*ima[b]+1e-12f; sp+=p; slp+=logf(p); bins++;}}
    if(!bins) return 1.0f;
    float ar=(float)(sp/(double)bins), geo=(float)exp(slp/(double)bins);
    return ar>1e-12f ? geo/ar : 0.0f;
}

static int nd_process(void* state,const OrpheusProcessContext* ctx){
    NoiseDetectorState* s=(NoiseDetectorState*)state;
    if(ctx->input_count<1||!ctx->inputs[0]) return ORPHEUS_ERR_INVALID_ARG;
    if(ctx->output_count<1||!ctx->outputs[0]) return ORPHEUS_ERR_INVALID_ARG;
    const float* in=(const float*)ctx->inputs[0]->data;
    float* out=(float*)ctx->outputs[0]->data;
    uint32_t frames=ctx->frame_count, ch=s->channels, fft=s->fft_size;

    memcpy(out,in,(size_t)frames*ch*sizeof(float));
    if(ctx->outputs[0]->frame_capacity>=frames) ctx->outputs[0]->frame_count=frames;

    double flat=0.0;
    for(uint32_t c=0;c<ch;++c) flat += block_flatness(in,frames,ch,c,fft,s->win,s->rea,s->ima);
    flat/=(double)ch;
    double ema=s->flatness_ema==0? flat : (s->flatness_ema*0.95 + flat*0.05);
    s->flatness_ema=ema; s->flatness=(float)ema;

    uint64_t clipped=0, clicks=0;
    double se=0.0; float peak=0.0f;
    for(uint32_t i=0;i<(uint32_t)frames*ch;++i){ float a=fabsf(in[i]); if(a>=s->clip_level)clipped++; double p=a; se+=p*p; if(a>peak)peak=a; }
    s->clipped_samples+=clipped; s->total_samples+=(uint64_t)frames*ch;
    double rms=sqrt(se/((uint32_t)frames*ch+1));
    /* 突刺（点状杂音/过载点）判定：按“同一通道”相邻样本比较，幅差超过阈值即计一次。说明：交错布局下若拿 in[i] 与 in[i-1] 比，会把左右声道差异误报为突刺；按通道比较后，正常音乐单样本滑动步很小，只有真正的突变才会超阈值。 */
    if (ch > 0 && frames > 2) {
        for (uint32_t i = 1; i < frames; ++i) {
            for (uint32_t c = 0; c < ch; ++c) {
                float d = fabsf(in[i*ch+c] - in[(i-1)*ch+c]);
                if (d > s->click_thres) clicks++;
            }
        }
    }
    s->clicks+=(uint32_t)clicks;
    s->clip_ratio= s->total_samples? (float)((double)s->clipped_samples/(double)s->total_samples) : 0.0f;
    double mean_p=se/((double)frames*ch+1.0);
    float rmsdb=10.0f*log10f((float)mean_p+1e-12f);
    float peakdb=20.0f*log10f(peak>1e-9f?peak:1e-9f);
    s->noise_floor_db = (s->flatness>0.5f)? rmsdb : (peakdb>rmsdb? (peakdb-10.0f) : rmsdb);

    { char* p=s->json_detail; size_t rem=sizeof(s->json_detail);
      int L=snprintf(p,rem,"{\"flatness\":%.3f,\"noise_floor_db\":%.2f,\"clicks\":%u,\"clip_pct\":%.4f}"
        ,(double)s->flatness,(double)s->noise_floor_db,s->clicks,(double)s->clip_ratio); (void)L; }
    return ORPHEUS_OK;
}
static int nd_set(void* st,const char* id,const OrpheusValue* v){(void)st;(void)id;(void)v;return ORPHEUS_ERR_UNSUPPORTED;}
static int nd_get(void* state,const char* id,OrpheusValue* v){
    NoiseDetectorState* s=(NoiseDetectorState*)state;
    if(!strcmp(id,"flatness")){v->type=ORPHEUS_VALUE_FLOAT;v->value.f32=s->flatness;return ORPHEUS_OK;}
    if(!strcmp(id,"noise_floor_db")){v->type=ORPHEUS_VALUE_FLOAT;v->value.f32=s->noise_floor_db;return ORPHEUS_OK;}
    if(!strcmp(id,"clicks")){v->type=ORPHEUS_VALUE_INT;v->value.i32=(int32_t)(s->clicks>0x7fffffff?0x7fffffff:s->clicks);return ORPHEUS_OK;}
    if(!strcmp(id,"clip_pct")){v->type=ORPHEUS_VALUE_FLOAT;v->value.f32=s->clip_ratio;return ORPHEUS_OK;}
    if(!strcmp(id,"detail")){v->type=ORPHEUS_VALUE_STRING;v->value.str=s->json_detail;return ORPHEUS_OK;}
    if(!strcmp(id,"channels")){v->type=ORPHEUS_VALUE_INT;v->value.i32=(int32_t)s->channels;return ORPHEUS_OK;}
    return ORPHEUS_ERR_NOT_FOUND;
}

static const OrpheusParameter nd_params[] = {
    { .id="channels", .name="\u901a\u9053\u6570", .type=ORPHEUS_VALUE_INT,
      .default_value={.type=ORPHEUS_VALUE_INT,.value.i32=2}, .min_i32=1,.max_i32=32,
      .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.readback=true,.persistent=true,.affects_signature=true },
    { .id="clip_level", .name="\u524a\u6ce2\u9608\u503c", .type=ORPHEUS_VALUE_FLOAT,
      .default_value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=0.999f},
      .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.readback=true,.persistent=true,.affects_signature=false },
    { .id="click_thres", .name="\u7a81\u523a\u9608\u503c", .type=ORPHEUS_VALUE_FLOAT,
      .default_value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=0.3f},
      .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.readback=true,.persistent=true,.affects_signature=false },
    { .id="flatness", .name="\u9891\u8c31\u5e73\u5766\u5ea6", .type=ORPHEUS_VALUE_FLOAT,
      .default_value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=0.0f},
      .update_policy=ORPHEUS_UPDATE_IMMEDIATE,.readback=true,.persistent=false,.affects_signature=false },
    { .id="noise_floor_db", .name="\u566a\u58f0\u5e95", .type=ORPHEUS_VALUE_FLOAT,
      .default_value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=0.0f},
      .update_policy=ORPHEUS_UPDATE_IMMEDIATE,.readback=true,.persistent=false,.affects_signature=false },
    { .id="clicks", .name="\u7a81\u523a\u8ba1\u6570", .type=ORPHEUS_VALUE_INT,
      .default_value={.type=ORPHEUS_VALUE_INT,.value.i32=0},
      .update_policy=ORPHEUS_UPDATE_IMMEDIATE,.readback=true,.persistent=false,.affects_signature=false },
    { .id="clip_pct", .name="\u524a\u6ce2\u5360\u6bd4", .type=ORPHEUS_VALUE_FLOAT,
      .default_value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=0.0f},
      .update_policy=ORPHEUS_UPDATE_IMMEDIATE,.readback=true,.persistent=false,.affects_signature=false },
    { .id="detail", .name="\u660e\u7ec6", .type=ORPHEUS_VALUE_STRING,
      .default_value={.type=ORPHEUS_VALUE_STRING,.value.str="{}"},
      .update_policy=ORPHEUS_UPDATE_IMMEDIATE,.readback=true,.persistent=false,.affects_signature=false }
};
static const OrpheusPort nd_ports[] = {
    { .id="in", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_AUDIO, .sample_format=ORPHEUS_FORMAT_F32,
      .channels=0,.sample_rate=0,.block_size=0,.is_variable=true,.channels_param="channels" },
    { .id="out", .direction=ORPHEUS_PORT_OUTPUT, .type=ORPHEUS_PORT_AUDIO, .sample_format=ORPHEUS_FORMAT_F32,
      .channels=0,.sample_rate=0,.block_size=0,.is_variable=true,.channels_param="channels" }
};
static const OrpheusComponentDescriptor nd_descriptor = {
    .id="orpheus.builtin.noise_detector", .version="1.0.0", .abi_version=ORPHEUS_ABI_VERSION,
    .ports=nd_ports, .port_count=2, .params=nd_params, .param_count=8,
    .state_size=sizeof(NoiseDetectorState), .scratch_size=0, .alignment=8,
    .latency_samples=0, .realtime_safe=true, .supports_inplace=false
};
static const OrpheusComponentDescriptor* nd_get_descriptor(void){ return &nd_descriptor; }

static int nd_reg(void* state,const OrpheusRegistry* reg){
    NoiseDetectorState* s=(NoiseDetectorState*)state;
    ORPHEUS_REG_SLOT(reg,s,channels,ORPHEUS_SLOT_SETTING,"channels","\u901a\u9053\u6570",ORPHEUS_VALUE_INT,
        .min_i32=1,.max_i32=32,.update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
        .flags=ORPHEUS_SLOT_PERSISTENT|ORPHEUS_SLOT_READBACK|ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    ORPHEUS_REG_SLOT(reg,s,clip_level,ORPHEUS_SLOT_SETTING,"clip_level","\u524a\u6ce2\u9608\u503c",ORPHEUS_VALUE_FLOAT,
        .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.flags=ORPHEUS_SLOT_PERSISTENT|ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg,s,click_thres,ORPHEUS_SLOT_SETTING,"click_thres","\u7a81\u523a\u9608\u503c",ORPHEUS_VALUE_FLOAT,
        .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.flags=ORPHEUS_SLOT_PERSISTENT|ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg,s,flatness,ORPHEUS_SLOT_PROBE,"flatness","\u9891\u8c31\u5e73\u5766\u5ea6",ORPHEUS_VALUE_FLOAT,
        .flags=ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg,s,noise_floor_db,ORPHEUS_SLOT_PROBE,"noise_floor_db","\u566a\u58f0\u5e95",ORPHEUS_VALUE_FLOAT,
        .flags=ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg,s,clicks,ORPHEUS_SLOT_PROBE,"clicks","\u7a81\u523a\u8ba1\u6570",ORPHEUS_VALUE_INT,
        .flags=ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg,s,clip_ratio,ORPHEUS_SLOT_PROBE,"clip_pct","\u524a\u6ce2\u5360\u6bd4",ORPHEUS_VALUE_FLOAT,
        .flags=ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg,s,json_detail,ORPHEUS_SLOT_PROBE,"detail","\u660e\u7ec6",ORPHEUS_VALUE_STRING,
        .flags=ORPHEUS_SLOT_READBACK);
    return ORPHEUS_OK;
}
static const OrpheusComponentInterface nd_interface = {
    .get_descriptor=nd_get_descriptor,.create=nd_create,.destroy=nd_destroy,
    .prepare=nd_prepare,.reset=nd_reset,.process=nd_process,
    .set_parameter=nd_set,.get_parameter=nd_get,
    .get_state_value=NULL,.register_slots=nd_reg
};

#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void){ return &nd_interface; }
