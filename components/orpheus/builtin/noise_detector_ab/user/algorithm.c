#include "component_state.h"
#include "component_api.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PI_F 3.14159265358979f

int orpheus_builtin_noise_detector_ab_normalize(orpheus_builtin_noise_detector_ab_Parameters* p, const char* key) {
    (void)key;
    return (p->fft_size&(p->fft_size-1)) ? ORPHEUS_ERR_INVALID_ARG : ORPHEUS_OK;
}

int orpheus_builtin_noise_detector_ab_prepare(orpheus_builtin_noise_detector_ab_State* s,
    const OrpheusConfig* config) {
    (void)config;
    uint32_t n=(uint32_t)s->fft_size;
    size_t bins=(size_t)s->channels*n/2;
    s->algorithm.sxx=calloc(bins,sizeof(float));s->algorithm.syy=calloc(bins,sizeof(float));
    s->algorithm.sxy_re=calloc(bins,sizeof(float));s->algorithm.sxy_im=calloc(bins,sizeof(float));
    s->algorithm.win=calloc(n,sizeof(float));
    s->algorithm.re=calloc((size_t)n*2,sizeof(float));s->algorithm.im=calloc((size_t)n*2,sizeof(float));
    if (!s->algorithm.sxx || !s->algorithm.syy || !s->algorithm.sxy_re || !s->algorithm.sxy_im ||
        !s->algorithm.win || !s->algorithm.re || !s->algorithm.im) return ORPHEUS_ERR_OUT_OF_MEMORY;
    for (uint32_t k=0;k<n;++k)
        s->algorithm.win[k]=0.5f-0.5f*cosf(2.0f*PI_F*(float)k/(float)(n-1));
    return ORPHEUS_OK;
}

int orpheus_builtin_noise_detector_ab_destroy(orpheus_builtin_noise_detector_ab_State* s) {
    free(s->algorithm.sxx);free(s->algorithm.syy);free(s->algorithm.sxy_re);free(s->algorithm.sxy_im);
    free(s->algorithm.win);free(s->algorithm.re);free(s->algorithm.im);
    s->algorithm.sxx=NULL;s->algorithm.syy=NULL;s->algorithm.sxy_re=NULL;s->algorithm.sxy_im=NULL;
    s->algorithm.win=NULL;s->algorithm.re=NULL;s->algorithm.im=NULL;
    return ORPHEUS_OK;
}

int orpheus_builtin_noise_detector_ab_reset(orpheus_builtin_noise_detector_ab_State* s) {
    if (!s->algorithm.sxx || !s->algorithm.syy || !s->algorithm.sxy_re || !s->algorithm.sxy_im)
        return ORPHEUS_ERR_INVALID_ARG;
    size_t bytes=(size_t)s->channels*s->fft_size/2*sizeof(float);
    memset(s->algorithm.sxx,0,bytes);memset(s->algorithm.syy,0,bytes);
    memset(s->algorithm.sxy_re,0,bytes);memset(s->algorithm.sxy_im,0,bytes);
    s->algorithm.total_windows=0;s->algorithm.noise_windows=0;s->algorithm.total_samples=0;
    s->algorithm.noisy_samples=0;s->algorithm.clicks_total=0;
    memset(s->algorithm.coherence,0,sizeof(s->algorithm.coherence));
    s->thd_n_db=0;s->noise_frames=0;s->noise_ratio=0;s->clicks=0;s->residue_pk=0;
    return ORPHEUS_OK;
}

int orpheus_builtin_noise_detector_ab_get_parameter(orpheus_builtin_noise_detector_ab_State* s,
    const char* key, OrpheusValue* value) {
    if (strcmp(key,"detail")) return ORPHEUS_ERR_NOT_FOUND;
    if (!isfinite(s->thd_n_db) || !isfinite(s->residue_pk) || !isfinite(s->algorithm.coherence[0]) ||
        !isfinite(s->algorithm.coherence[1]) || !isfinite(s->algorithm.coherence[2]))
        return ORPHEUS_ERR_INVALID_ARG;
    int length=snprintf(s->algorithm.json_detail,sizeof(s->algorithm.json_detail),
        "{\"thd_n_db\":%.2f,\"coh_lo\":%.3f,\"coh_mid\":%.3f,\"coh_hi\":%.3f,"
        "\"noise_frames\":%llu,\"total_frames\":%llu,\"noisy_samples\":%llu,\"total_samples\":%llu,"
        "\"clicks\":%llu,\"residue_pk\":%.4f}",
        (double)s->thd_n_db,(double)s->algorithm.coherence[0],(double)s->algorithm.coherence[1],
        (double)s->algorithm.coherence[2],(unsigned long long)s->algorithm.noise_windows,
        (unsigned long long)s->algorithm.total_windows,(unsigned long long)s->algorithm.noisy_samples,
        (unsigned long long)s->algorithm.total_samples,(unsigned long long)s->algorithm.clicks_total,
        (double)s->residue_pk);
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
        float angle=-2.0f*PI_F/(float)len,wr=cosf(angle),wi=sinf(angle);
        for (uint32_t i=0;i<n;i+=len) {
            float cwr=1,cwi=0;
            for (uint32_t k=0;k<len/2;++k) {
                uint32_t a=i+k,b=i+k+len/2;
                float tr=cwr*re[b]-cwi*im[b],ti=cwr*im[b]+cwi*re[b];
                re[b]=re[a]-tr;im[b]=im[a]-ti;re[a]+=tr;im[a]+=ti;
                float next=cwr*wr-cwi*wi;cwi=cwr*wi+cwi*wr;cwr=next;
            }
        }
    }
}

static float coherence(float xx, float yy, float re, float im) {
    float den=xx*yy,mag=re*re+im*im;
    float value=den>1e-12f ? mag/den : 0;
    return value>1 ? 1 : value;
}

static float thd(double residual, double power) {
    if (power<=1e-12) return -120.0f;
    double ratio=residual/power;
    return 10.0f*log10f((float)(ratio>1e-12 ? ratio : 1e-12));
}

static bool overlap(const void* a, size_t na, const void* b, size_t nb) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return x>=y ? x-y<nb : y-x<na;
}

static bool private_overlap(const orpheus_builtin_noise_detector_ab_State* s, const void* data, size_t bytes) {
    size_t bins=(size_t)s->channels*s->fft_size/2*sizeof(float),window=(size_t)s->fft_size*sizeof(float);
    return overlap(data,bytes,s,sizeof(*s)) || overlap(data,bytes,s->algorithm.sxx,bins) ||
        overlap(data,bytes,s->algorithm.syy,bins) || overlap(data,bytes,s->algorithm.sxy_re,bins) ||
        overlap(data,bytes,s->algorithm.sxy_im,bins) || overlap(data,bytes,s->algorithm.win,window) ||
        overlap(data,bytes,s->algorithm.re,window*2) || overlap(data,bytes,s->algorithm.im,window*2);
}

int orpheus_builtin_noise_detector_ab_algorithm(orpheus_builtin_noise_detector_ab_State* s,
    orpheus_builtin_noise_detector_ab_IO* io, const OrpheusProcessContext* ctx) {
    if (!ctx->frame_count) return ORPHEUS_OK;
    if (ctx->trigger_input!=io->in.input_index) return ORPHEUS_ERR_INVALID_ARG;
    uint32_t n=(uint32_t)s->fft_size,h=n/2,channels=(uint32_t)s->channels;
    uint32_t frames=n*(uint32_t)s->batches;
    if (!s->algorithm.sxx || !s->algorithm.syy || !s->algorithm.sxy_re || !s->algorithm.sxy_im ||
        !s->algorithm.win || !s->algorithm.re || !s->algorithm.im || !io->ref.buffer || !io->in.buffer ||
        io->ref.buffer->frame_count!=frames || io->in.buffer->frame_count!=frames)
        return ORPHEUS_ERR_INVALID_ARG;
    size_t bytes=(size_t)frames*channels*sizeof(float);
    if (private_overlap(s,io->ref.samples,bytes) || private_overlap(s,io->in.samples,bytes) ||
        (io->out.buffer && (private_overlap(s,io->out.samples,bytes) ||
         overlap(io->ref.samples,bytes,io->out.samples,bytes) || overlap(io->in.samples,bytes,io->out.samples,bytes))))
        return ORPHEUS_ERR_INVALID_ARG;
    uint64_t samples=(uint64_t)frames*channels;
    if (s->algorithm.total_windows>UINT64_MAX-(uint32_t)s->batches ||
        s->algorithm.total_samples>UINT64_MAX-samples ||
        s->algorithm.noise_windows>s->algorithm.total_windows ||
        s->algorithm.noisy_samples>s->algorithm.total_samples ||
        s->algorithm.clicks_total>s->algorithm.total_samples) return ORPHEUS_ERR_PROCESSING;
    float alpha=1.0f/(float)s->smoothing;
    double accumulated_power=0,accumulated_residual=0;
    uint64_t noise_windows=0;
    for (uint32_t w=0;w<(uint32_t)s->batches;++w) {
        size_t off=(size_t)w*n;
        for (uint32_t c=0;c<channels;++c) {
            float* xr=s->algorithm.re;float* xi=s->algorithm.im;
            float* yr=xr+n;float* yi=xi+n;
            for (uint32_t k=0;k<n;++k) {
                xr[k]=io->ref.samples[(off+k)*channels+c]*s->algorithm.win[k];xi[k]=0;
                yr[k]=io->in.samples[(off+k)*channels+c]*s->algorithm.win[k];yi[k]=0;
            }
            fft(xr,xi,n);fft(yr,yi,n);
            for (uint32_t k=0;k<h;++k) {
                size_t index=(size_t)c*h+k;
                float xx=xr[k]*xr[k]+xi[k]*xi[k],yy=yr[k]*yr[k]+yi[k]*yi[k];
                float re=xr[k]*yr[k]+xi[k]*yi[k],im=xi[k]*yr[k]-xr[k]*yi[k];
                s->algorithm.sxx[index]+=alpha*(xx-s->algorithm.sxx[index]);
                s->algorithm.syy[index]+=alpha*(yy-s->algorithm.syy[index]);
                s->algorithm.sxy_re[index]+=alpha*(re-s->algorithm.sxy_re[index]);
                s->algorithm.sxy_im[index]+=alpha*(im-s->algorithm.sxy_im[index]);
            }
        }
        double power=0,residual=0;
        for (uint32_t c=0;c<channels;++c) for (uint32_t k=1;k<h;++k) {
            size_t index=(size_t)c*h+k;
            float yy=s->algorithm.syy[index];
            float gamma=coherence(s->algorithm.sxx[index],yy,
                s->algorithm.sxy_re[index],s->algorithm.sxy_im[index]);
            power+=yy;residual+=(1.0f-gamma)*yy;
        }
        accumulated_power+=power;accumulated_residual+=residual;
        if (thd(residual,power)>s->threshold_db) noise_windows++;
    }
    s->thd_n_db=thd(accumulated_residual,accumulated_power);
    uint64_t noisy=0,clicks=0;
    float peak=s->residue_pk;
    for (size_t i=0;i<(size_t)samples;++i) {
        float residue=fabsf(io->ref.samples[i]-io->in.samples[i]);
        if (residue>s->time_thres) noisy++;
        if (residue>0.2f) clicks++;
        if (residue>peak) peak=residue;
    }
    s->residue_pk=peak;
    s->algorithm.total_samples+=samples;s->algorithm.noisy_samples+=noisy;s->algorithm.clicks_total+=clicks;
    s->algorithm.total_windows+=(uint32_t)s->batches;s->algorithm.noise_windows+=noise_windows;
    s->noise_frames=s->algorithm.noise_windows>INT32_MAX ? INT32_MAX : (int32_t)s->algorithm.noise_windows;
    s->clicks=s->algorithm.clicks_total>INT32_MAX ? INT32_MAX : (int32_t)s->algorithm.clicks_total;
    s->noise_ratio=(float)((double)s->algorithm.noise_windows/(double)s->algorithm.total_windows);
    uint32_t low=h/4;if (low<1) low=1;
    uint32_t middle=3*h/4;if (middle<=low) middle=low+1;
    double bands[3]={0};uint32_t counts[3]={0};
    for (uint32_t c=0;c<channels;++c) for (uint32_t k=1;k<h;++k) {
        size_t index=(size_t)c*h+k;
        unsigned band=k<low ? 0 : (k<middle ? 1 : 2);
        bands[band]+=coherence(s->algorithm.sxx[index],s->algorithm.syy[index],
            s->algorithm.sxy_re[index],s->algorithm.sxy_im[index]);
        counts[band]++;
    }
    for (unsigned band=0;band<3;++band)
        s->algorithm.coherence[band]=counts[band] ? (float)(bands[band]/counts[band]) : 0;
    if (!io->out.buffer) return ORPHEUS_OK;
    memcpy(io->out.samples,io->in.samples,bytes);io->out.buffer->frame_count=frames;
    return orpheus_builtin_noise_detector_ab_emit_out(ctx,&io->out);
}
