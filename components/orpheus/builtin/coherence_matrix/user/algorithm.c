#include "component_state.h"
#include "component_api.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int orpheus_builtin_coherence_matrix_normalize(orpheus_builtin_coherence_matrix_Parameters* p,
    const char* key) {
    (void)key;
    return (p->fft_size&(p->fft_size-1)) ? ORPHEUS_ERR_INVALID_ARG : ORPHEUS_OK;
}

int orpheus_builtin_coherence_matrix_prepare(orpheus_builtin_coherence_matrix_State* s,
    const OrpheusConfig* config) {
    (void)config;
    size_t n=(size_t)s->channels,h=(size_t)s->fft_size/2;
    s->algorithm.gxx=calloc(n*h,sizeof(float));
    s->algorithm.gxy_re=calloc(n*n*h,sizeof(float));
    s->algorithm.gxy_im=calloc(n*n*h,sizeof(float));
    s->algorithm.re=calloc(n*h*2,sizeof(float));
    s->algorithm.im=calloc(n*h*2,sizeof(float));
    return s->algorithm.gxx && s->algorithm.gxy_re && s->algorithm.gxy_im &&
        s->algorithm.re && s->algorithm.im ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}

int orpheus_builtin_coherence_matrix_destroy(orpheus_builtin_coherence_matrix_State* s) {
    free(s->algorithm.gxx);free(s->algorithm.gxy_re);free(s->algorithm.gxy_im);
    free(s->algorithm.re);free(s->algorithm.im);
    s->algorithm.gxx=NULL;s->algorithm.gxy_re=NULL;s->algorithm.gxy_im=NULL;
    s->algorithm.re=NULL;s->algorithm.im=NULL;
    return ORPHEUS_OK;
}

int orpheus_builtin_coherence_matrix_reset(orpheus_builtin_coherence_matrix_State* s) {
    if (!s->algorithm.gxx || !s->algorithm.gxy_re || !s->algorithm.gxy_im ||
        !s->algorithm.re || !s->algorithm.im) return ORPHEUS_ERR_INVALID_ARG;
    size_t n=(size_t)s->channels,h=(size_t)s->fft_size/2;
    memset(s->algorithm.gxx,0,n*h*sizeof(float));
    memset(s->algorithm.gxy_re,0,n*n*h*sizeof(float));
    memset(s->algorithm.gxy_im,0,n*n*h*sizeof(float));
    memset(s->algorithm.matrix,0,sizeof(s->algorithm.matrix));
    memset(s->algorithm.hist,0,sizeof(s->algorithm.hist));
    s->algorithm.hist_pos=0;s->algorithm.hist_count=0;
    return ORPHEUS_OK;
}

int orpheus_builtin_coherence_matrix_get_parameter(orpheus_builtin_coherence_matrix_State* s,
    const char* key, OrpheusValue* value) {
    bool history=!strcmp(key,"history");
    if (!history && strcmp(key,"coherence")) return ORPHEUS_ERR_NOT_FOUND;
    uint32_t n=(uint32_t)s->channels,h=(uint32_t)s->fft_size/2;
    uint32_t count=history ? s->algorithm.hist_count : n*n;
    char* json=s->algorithm.json;
    size_t capacity=sizeof(s->algorithm.json),used=0;
    int length=history ? snprintf(json,capacity,"[") :
        snprintf(json,capacity,"{\"n\":%u,\"bins\":%u,\"matrix\":[",n,h);
    if (length<0 || (size_t)length>=capacity) return ORPHEUS_ERR_PROCESSING;
    used=(size_t)length;
    for (uint32_t i=0;i<count;++i) {
        float current=history ? s->algorithm.hist[(s->algorithm.hist_pos+64-count+i)%64]
            : s->algorithm.matrix[i];
        if (!isfinite(current)) return ORPHEUS_ERR_INVALID_ARG;
        length=snprintf(json+used,capacity-used,i ? ",%.4g" : "%.4g",(double)current);
        if (length<0 || (size_t)length>=capacity-used) return ORPHEUS_ERR_PROCESSING;
        used+=(size_t)length;
    }
    length=snprintf(json+used,capacity-used,history ? "]" : "]}");
    if (length<0 || (size_t)length>=capacity-used) return ORPHEUS_ERR_PROCESSING;
    value->type=ORPHEUS_VALUE_STRING;value->value.str=json;
    return ORPHEUS_OK;
}

static void fft_radix2(float* re, float* im, uint32_t n) {
    for (uint32_t i=1,j=0;i<n;++i) {
        uint32_t bit=n>>1;
        for (;j&bit;bit>>=1) j^=bit;
        j^=bit;
        if (i<j) {
            float t=re[i];re[i]=re[j];re[j]=t;
            t=im[i];im[i]=im[j];im[j]=t;
        }
    }
    for (uint32_t len=2;len<=n;len<<=1) {
        float angle=-2.0f*3.14159265358979323846f/(float)len;
        float wr=cosf(angle),wi=sinf(angle);
        for (uint32_t i=0;i<n;i+=len) {
            float cwr=1,cwi=0;
            for (uint32_t k=0;k<len/2;++k) {
                uint32_t a=i+k,b=i+k+len/2;
                float tr=cwr*re[b]-cwi*im[b],ti=cwr*im[b]+cwi*re[b];
                re[b]=re[a]-tr;im[b]=im[a]-ti;
                re[a]+=tr;im[a]+=ti;
                float next=cwr*wr-cwi*wi;
                cwi=cwr*wi+cwi*wr;cwr=next;
            }
        }
    }
}

static bool overlap(const void* a, size_t na, const void* b, size_t nb) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return x>=y ? x-y<nb : y-x<na;
}

static bool private_overlap(const orpheus_builtin_coherence_matrix_State* s, const void* data, size_t bytes) {
    size_t n=(size_t)s->channels,h=(size_t)s->fft_size/2;
    return overlap(data,bytes,s,sizeof(*s)) || overlap(data,bytes,s->algorithm.gxx,n*h*sizeof(float)) ||
        overlap(data,bytes,s->algorithm.gxy_re,n*n*h*sizeof(float)) ||
        overlap(data,bytes,s->algorithm.gxy_im,n*n*h*sizeof(float)) ||
        overlap(data,bytes,s->algorithm.re,n*h*2*sizeof(float)) ||
        overlap(data,bytes,s->algorithm.im,n*h*2*sizeof(float));
}

int orpheus_builtin_coherence_matrix_algorithm(orpheus_builtin_coherence_matrix_State* s,
    orpheus_builtin_coherence_matrix_IO* io, const OrpheusProcessContext* ctx) {
    if (!ctx->frame_count) return ORPHEUS_OK;
    uint32_t n=(uint32_t)s->channels,frames=(uint32_t)s->fft_size,h=frames/2;
    if (!s->algorithm.gxx || !s->algorithm.gxy_re || !s->algorithm.gxy_im ||
        !s->algorithm.re || !s->algorithm.im || !io->in.buffer || !io->out.buffer ||
        io->in.buffer->frame_count!=frames) return ORPHEUS_ERR_INVALID_ARG;
    size_t bytes=(size_t)n*frames*sizeof(float);
    if (private_overlap(s,io->in.samples,bytes) || private_overlap(s,io->out.samples,bytes) ||
        (io->in.samples!=io->out.samples && overlap(io->in.samples,bytes,io->out.samples,bytes)))
        return ORPHEUS_ERR_INVALID_ARG;
    if (io->in.samples!=io->out.samples) memcpy(io->out.samples,io->in.samples,bytes);
    for (uint32_t c=0;c<n;++c) {
        float* re=s->algorithm.re+(size_t)c*frames;
        float* im=s->algorithm.im+(size_t)c*frames;
        for (uint32_t f=0;f<frames;++f) re[f]=io->in.samples[f*n+c];
        memset(im,0,frames*sizeof(float));
        fft_radix2(re,im,frames);
    }
    float alpha=1.0f/(float)s->smoothing;
    for (uint32_t k=0;k<h;++k) for (uint32_t i=0;i<n;++i) {
        float xi_re=s->algorithm.re[(size_t)i*frames+k],xi_im=s->algorithm.im[(size_t)i*frames+k];
        float power=xi_re*xi_re+xi_im*xi_im;
        s->algorithm.gxx[i*h+k]+=alpha*(power-s->algorithm.gxx[i*h+k]);
        for (uint32_t j=0;j<n;++j) {
            float xj_re=s->algorithm.re[(size_t)j*frames+k],xj_im=s->algorithm.im[(size_t)j*frames+k];
            float re=xi_re*xj_re+xi_im*xj_im,im=xi_im*xj_re-xi_re*xj_im;
            size_t off=((size_t)i*n+j)*h+k;
            s->algorithm.gxy_re[off]+=alpha*(re-s->algorithm.gxy_re[off]);
            s->algorithm.gxy_im[off]+=alpha*(im-s->algorithm.gxy_im[off]);
        }
    }
    float mean=0;
    for (uint32_t i=0;i<n;++i) for (uint32_t j=0;j<n;++j) {
        double sum=0;
        for (uint32_t k=0;k<h;++k) {
            size_t off=((size_t)i*n+j)*h+k;
            float re=s->algorithm.gxy_re[off],im=s->algorithm.gxy_im[off];
            float magnitude=re*re+im*im,denom=s->algorithm.gxx[i*h+k]*s->algorithm.gxx[j*h+k];
            sum+=denom>1e-12f ? magnitude/denom : 0.0;
        }
        float current=(float)(sum/(double)h);
        if (current>1) current=1;
        s->algorithm.matrix[i*n+j]=current;
        if (i!=j) mean+=current;
    }
    s->algorithm.hist[s->algorithm.hist_pos]=mean/(float)(n*(n-1));
    s->algorithm.hist_pos=(s->algorithm.hist_pos+1)%64;
    if (s->algorithm.hist_count<64) s->algorithm.hist_count++;
    io->out.buffer->frame_count=frames;
    return orpheus_builtin_coherence_matrix_emit_out(ctx,&io->out);
}
