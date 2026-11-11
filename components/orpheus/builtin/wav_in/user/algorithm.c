#include "component_state.h"
#include "component_api.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif

#define WAV_IN_MAX_FRAMES (1024u*1024u*16u)

static uint16_t read_u16(const uint8_t* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1]<<8);
}
static uint32_t read_u32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}
static int seek_to(FILE* f,uint64_t offset) {
#ifdef _WIN32
    return offset>INT64_MAX ? -1 : _fseeki64(f,(int64_t)offset,SEEK_SET);
#else
    return offset>LONG_MAX ? -1 : fseek(f,(long)offset,SEEK_SET);
#endif
}
static int file_size(FILE* f,uint64_t* size) {
#ifdef _WIN32
    if(_fseeki64(f,0,SEEK_END)) return ORPHEUS_ERR_PROCESSING;
    int64_t value=_ftelli64(f);
#else
    if(fseek(f,0,SEEK_END)) return ORPHEUS_ERR_PROCESSING;
    long value=ftell(f);
#endif
    if(value<0) return ORPHEUS_ERR_PROCESSING;
    *size=(uint64_t)value;
    return seek_to(f,0) ? ORPHEUS_ERR_PROCESSING : ORPHEUS_OK;
}
static int open_file(const char* path,FILE** file) {
    *file=NULL;
#ifdef _WIN32
    int length=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path,-1,NULL,0);
    if(length<=0) return ORPHEUS_ERR_INVALID_ARG;
    wchar_t* wide=malloc((size_t)length*sizeof(*wide));
    if(!wide) return ORPHEUS_ERR_OUT_OF_MEMORY;
    if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path,-1,wide,length)) {
        free(wide);return ORPHEUS_ERR_INVALID_ARG;
    }
    *file=_wfopen(wide,L"rb");
    free(wide);
#else
    *file=fopen(path,"rb");
#endif
    return *file ? ORPHEUS_OK : ORPHEUS_ERR_NOT_FOUND;
}

static int read_wave(FILE* f,int32_t channels,float** samples,uint32_t* total_frames) {
    uint64_t size;
    int rc=file_size(f,&size);
    if(rc) return rc;
    uint8_t header[16];
    if(size<12 || fread(header,1,12,f)!=12 || memcmp(header,"RIFF",4) ||
       memcmp(header+8,"WAVE",4)) return ORPHEUS_ERR_INVALID_ARG;
    uint64_t end=(uint64_t)read_u32(header+4)+8;
    if(end<12 || end>size) return ORPHEUS_ERR_INVALID_ARG;
    uint64_t offset=12,data_offset=0;
    uint32_t data_size=0;
    uint16_t format=0,file_channels=0,bits=0,alignment=0;
    bool have_format=false;
    while(offset<end) {
        if(end-offset<8 || seek_to(f,offset) || fread(header,1,8,f)!=8)
            return ORPHEUS_ERR_INVALID_ARG;
        uint32_t length=read_u32(header+4);
        uint64_t next=offset+8+(uint64_t)length+(length&1u);
        if(next>end) return ORPHEUS_ERR_INVALID_ARG;
        if(!memcmp(header,"fmt ",4)) {
            if(have_format || length<16 || fread(header,1,16,f)!=16) return ORPHEUS_ERR_INVALID_ARG;
            format=read_u16(header);file_channels=read_u16(header+2);
            uint32_t rate=read_u32(header+4),byte_rate=read_u32(header+8);
            alignment=read_u16(header+12);bits=read_u16(header+14);
            if(!file_channels || !rate || !((format==1 && (bits==16 || bits==32)) || (format==3 && bits==32)) ||
               (uint64_t)file_channels*(bits/8)!=alignment ||
               (uint64_t)rate*alignment!=byte_rate) return ORPHEUS_ERR_INVALID_ARG;
            have_format=true;
        } else if(!memcmp(header,"data",4)) {
            if(data_offset) return ORPHEUS_ERR_INVALID_ARG;
            data_offset=offset+8;data_size=length;
        }
        offset=next;
    }
    if(!have_format || !data_offset || data_size%alignment) return ORPHEUS_ERR_INVALID_ARG;
    uint32_t frames=data_size/alignment;
    if(frames>WAV_IN_MAX_FRAMES || (uint64_t)frames*(uint32_t)channels>SIZE_MAX/sizeof(float))
        return ORPHEUS_ERR_INVALID_ARG;
    if(!frames) { *samples=NULL;*total_frames=0;return ORPHEUS_OK; }
    uint8_t* raw=malloc(data_size);
    if(!raw) return ORPHEUS_ERR_OUT_OF_MEMORY;
    if(seek_to(f,data_offset) || fread(raw,1,data_size,f)!=data_size) {
        free(raw);return ORPHEUS_ERR_INVALID_ARG;
    }
    float* decoded=malloc((size_t)frames*(uint32_t)channels*sizeof(float));
    if(!decoded) { free(raw);return ORPHEUS_ERR_OUT_OF_MEMORY; }
    for(uint32_t n=0;n<frames;n++) for(int32_t c=0;c<channels;c++) {
        uint32_t source=c<file_channels ? (uint32_t)c : 0;
        const uint8_t* value=raw+(size_t)n*alignment+(size_t)source*(bits/8);
        float* output=decoded+(size_t)n*(uint32_t)channels+(uint32_t)c;
        if(format==3) {
            uint32_t word=read_u32(value);
            memcpy(output,&word,sizeof(word));
        } else if(bits==16) *output=(int16_t)read_u16(value)/32768.0f;
        else *output=(int32_t)read_u32(value)/2147483648.0f;
    }
    free(raw);
    *samples=decoded;*total_frames=frames;
    return ORPHEUS_OK;
}

int orpheus_builtin_wav_in_prepare(orpheus_builtin_wav_in_State* s,const OrpheusConfig* config) {
    (void)config;
    if(!s->file_path[0]) return ORPHEUS_OK;
    FILE* file;
    int rc=open_file(s->file_path,&file);
    if(rc) return rc;
    rc=read_wave(file,s->channels,&s->algorithm.samples,&s->algorithm.total_frames);
    if(fclose(file) && !rc) rc=ORPHEUS_ERR_PROCESSING;
    return rc;
}
int orpheus_builtin_wav_in_reset(orpheus_builtin_wav_in_State* s) {
    s->algorithm.position=0;
    return ORPHEUS_OK;
}
int orpheus_builtin_wav_in_destroy(orpheus_builtin_wav_in_State* s) {
    free(s->algorithm.samples);
    s->algorithm.samples=NULL;
    return ORPHEUS_OK;
}
static bool overlap(const void* a,size_t na,const void* b,size_t nb) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return na && nb && (x>=y ? x-y<nb : y-x<na);
}
int orpheus_builtin_wav_in_algorithm(orpheus_builtin_wav_in_State* s,
    orpheus_builtin_wav_in_IO* io,const OrpheusProcessContext* ctx) {
    if(!io->out.buffer || s->algorithm.position>s->algorithm.total_frames ||
       (s->algorithm.total_frames && !s->algorithm.samples)) return ORPHEUS_ERR_INVALID_ARG;
    size_t bytes=(size_t)ctx->frame_count*s->channels*sizeof(float);
    if(overlap(io->out.samples,bytes,s,sizeof(*s)) ||
       overlap(io->out.samples,bytes,s->algorithm.samples,(size_t)s->algorithm.total_frames*s->channels*sizeof(float)))
        return ORPHEUS_ERR_INVALID_ARG;
    uint32_t remaining=s->algorithm.total_frames-s->algorithm.position;
    uint32_t count=ctx->frame_count<remaining ? ctx->frame_count : remaining;
    size_t copy_bytes=(size_t)count*s->channels*sizeof(float);
    if(copy_bytes) memcpy(io->out.samples,s->algorithm.samples+(size_t)s->algorithm.position*s->channels,copy_bytes);
    if(bytes>copy_bytes) memset((uint8_t*)io->out.samples+copy_bytes,0,bytes-copy_bytes);
    s->algorithm.position+=count;
    io->out.buffer->frame_count=ctx->frame_count;
    int rc=orpheus_builtin_wav_in_emit_out(ctx,&io->out);
    return rc ? rc : orpheus_builtin_wav_in_emit_tick(ctx,&io->tick);
}
