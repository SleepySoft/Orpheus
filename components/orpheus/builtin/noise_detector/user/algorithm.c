#include "component_state.h"
#include "component_api.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PI_F 3.14159265358979f

int orpheus_builtin_noise_detector_normalize(orpheus_builtin_noise_detector_Parameters* p, const char* key) {
    (void)key;
    return (p->fft_size&(p->fft_size-1)) ? ORPHEUS_ERR_INVALID_ARG : ORPHEUS_OK;
}

int orpheus_builtin_noise_detector_prepare(orpheus_builtin_noise_detector_State* s, const OrpheusConfig* config) {
    (void)config;
    uint32_t n=(uint32_t)s->fft_size;
    s->algorithm.win=calloc(n,sizeof(float));
    s->algorithm.re=calloc(n,sizeof(float));
    s->algorithm.im=calloc(n,sizeof(float));
    if (!s->algorithm.win || !s->algorithm.re || !s->algorithm.im) return ORPHEUS_ERR_OUT_OF_MEMORY;
    for (uint32_t k=0;k<n;++k)
        s->algorithm.win[k]=0.5f-0.5f*cosf(2.0f*PI_F*(float)k/(float)(n-1));
    return ORPHEUS_OK;
}

int orpheus_builtin_noise_detector_destroy(orpheus_builtin_noise_detector_State* s) {
    free(s->algorithm.win);free(s->algorithm.re);free(s->algorithm.im);
    s->algorithm.win=NULL;s->algorithm.re=NULL;s->algorithm.im=NULL;
    return ORPHEUS_OK;
}

int orpheus_builtin_noise_detector_reset(orpheus_builtin_noise_detector_State* s) {
    s->algorithm.flatness_ema=0;s->algorithm.total_samples=0;
    s->algorithm.clipped_samples=0;s->algorithm.clicks_total=0;
    s->flatness=0;s->noise_floor_db=0;s->clicks=0;s->clip_pct=0;
    return ORPHEUS_OK;
}

int orpheus_builtin_noise_detector_get_parameter(orpheus_builtin_noise_detector_State* s,
    const char* key, OrpheusValue* value) {
    if (strcmp(key,"detail")) return ORPHEUS_ERR_NOT_FOUND;
    if (!isfinite(s->flatness) || !isfinite(s->noise_floor_db) || !isfinite(s->clip_pct))
        return ORPHEUS_ERR_INVALID_ARG;
    int length=snprintf(s->algorithm.json_detail,sizeof(s->algorithm.json_detail),
        "{\"flatness\":%.3f,\"noise_floor_db\":%.2f,\"clicks\":%llu,\"clip_pct\":%.4f}",
        (double)s->flatness,(double)s->noise_floor_db,(unsigned long long)s->algorithm.clicks_total,
        (double)s->clip_pct);
    if (length<0 || (size_t)length>=sizeof(s->algorithm.json_detail)) return ORPHEUS_ERR_PROCESSING;
    value->type=ORPHEUS_VALUE_STRING;value->value.str=s->algorithm.json_detail;
    return ORPHEUS_OK;
}

static void fft(float* re, float* im, uint32_t n) {
    for (uint32_t i=1,j=0;i<n;++i) {
        uint32_t b=n>>1;
        for (;j&b;b>>=1) j^=b;
        j^=b;
        if (i<j) {
            float t=re[i];re[i]=re[j];re[j]=t;
            t=im[i];im[i]=im[j];im[j]=t;
        }
    }
    for (uint32_t len=2;len<=n;len<<=1) {
        float angle=-2.0f*PI_F/(float)len,cw=cosf(angle),sw=sinf(angle);
        for (uint32_t i=0;i<n;i+=len) {
            float cwr=1,cwi=0;
            for (uint32_t k=0;k<len/2;++k) {
                uint32_t a=i+k,b=i+k+len/2;
                float tr=cwr*re[b]-cwi*im[b],ti=cwr*im[b]+cwi*re[b];
                re[b]=re[a]-tr;im[b]=im[a]-ti;re[a]+=tr;im[a]+=ti;
                float next=cwr*cw-cwi*sw;
                cwi=cwr*sw+cwi*cw;cwr=next;
            }
        }
    }
}

static float block_flatness(const float* in, uint32_t frames, uint32_t channels, uint32_t channel,
    uint32_t n, const float* win, float* re, float* im) {
    double power=0,logs=0;
    uint32_t bins=0;
    for (uint32_t w=0;w<frames/n;++w) {
        size_t off=(size_t)w*n;
        for (uint32_t k=0;k<n;++k) {
            re[k]=in[(off+k)*channels+channel]*win[k];im[k]=0;
        }
        fft(re,im,n);
        for (uint32_t b=1;b<n/2;++b) {
            float p=re[b]*re[b]+im[b]*im[b]+1e-12f;
            power+=p;logs+=logf(p);bins++;
        }
    }
    float arithmetic=(float)(power/(double)bins),geometric=(float)exp(logs/(double)bins);
    return arithmetic>1e-12f ? geometric/arithmetic : 0;
}

static bool overlap(const void* a, size_t na, const void* b, size_t nb) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return x>=y ? x-y<nb : y-x<na;
}

static bool private_overlap(const orpheus_builtin_noise_detector_State* s, const void* data, size_t bytes) {
    size_t scratch_bytes=(size_t)s->fft_size*sizeof(float);
    return overlap(data,bytes,s,sizeof(*s)) || overlap(data,bytes,s->algorithm.win,scratch_bytes) ||
        overlap(data,bytes,s->algorithm.re,scratch_bytes) || overlap(data,bytes,s->algorithm.im,scratch_bytes);
}

int orpheus_builtin_noise_detector_algorithm(orpheus_builtin_noise_detector_State* s,
    orpheus_builtin_noise_detector_IO* io, const OrpheusProcessContext* ctx) {
    if (!ctx->frame_count) return ORPHEUS_OK;
    uint32_t frames=(uint32_t)s->fft_size*(uint32_t)s->batches,channels=(uint32_t)s->channels;
    if (!s->algorithm.win || !s->algorithm.re || !s->algorithm.im || !io->in.buffer || !io->out.buffer ||
        io->in.buffer->frame_count!=frames) return ORPHEUS_ERR_INVALID_ARG;
    size_t bytes=(size_t)frames*channels*sizeof(float);
    if (private_overlap(s,io->in.samples,bytes) || private_overlap(s,io->out.samples,bytes) ||
        overlap(io->in.samples,bytes,io->out.samples,bytes)) return ORPHEUS_ERR_INVALID_ARG;
    uint64_t samples=(uint64_t)frames*channels,clipped=0,clicks=0;
    double energy=0;
    float peak=0;
    for (size_t i=0;i<(size_t)samples;++i) {
        float a=fabsf(io->in.samples[i]);
        if (a>=s->clip_level) clipped++;
        double p=a;energy+=p*p;
        if (a>peak) peak=a;
    }
    for (uint32_t f=1;f<frames;++f) for (uint32_t c=0;c<channels;++c)
        if (fabsf(io->in.samples[f*channels+c]-io->in.samples[(f-1)*channels+c])>s->click_thres)
            clicks++;
    if (s->algorithm.total_samples>UINT64_MAX-samples ||
        s->algorithm.clipped_samples>UINT64_MAX-clipped || s->algorithm.clicks_total>UINT64_MAX-clicks)
        return ORPHEUS_ERR_PROCESSING;
    double flat=0;
    for (uint32_t c=0;c<channels;++c)
        flat+=block_flatness(io->in.samples,frames,channels,c,(uint32_t)s->fft_size,
            s->algorithm.win,s->algorithm.re,s->algorithm.im);
    flat/=(double)channels;
    s->algorithm.flatness_ema=s->algorithm.flatness_ema==0 ? flat : s->algorithm.flatness_ema*0.95+flat*0.05;
    s->flatness=(float)s->algorithm.flatness_ema;
    s->algorithm.clipped_samples+=clipped;s->algorithm.total_samples+=samples;s->algorithm.clicks_total+=clicks;
    s->clicks=s->algorithm.clicks_total>INT32_MAX ? INT32_MAX : (int32_t)s->algorithm.clicks_total;
    s->clip_pct=(float)((double)s->algorithm.clipped_samples/(double)s->algorithm.total_samples);
    float rmsdb=10.0f*log10f((float)(energy/((double)frames*channels+1.0))+1e-12f);
    float peakdb=20.0f*log10f(peak>1e-9f ? peak : 1e-9f);
    s->noise_floor_db=s->flatness>0.5f ? rmsdb : (peakdb>rmsdb ? peakdb-10.0f : rmsdb);
    memcpy(io->out.samples,io->in.samples,bytes);
    io->out.buffer->frame_count=frames;
    return orpheus_builtin_noise_detector_emit_out(ctx,&io->out);
}
