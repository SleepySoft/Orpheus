#include "component_state.h"
#include "component_api.h"
#define MA_NO_DEVICE_IO
#define MA_NO_ENGINE
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENCODING
/* MA_API不修饰原子全局锁，锁也须隔离，避免与设备宿主重复定义。 */
#define MA_API static
#define ma_atomic_global_lock orpheus_mp3_decoder_global_lock
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include <stdlib.h>
#include <string.h>

#define MP3_IN_MAX_FRAMES (1024u*1024u*16u)

static int decoder_error(ma_result error) {
    if(error==MA_OUT_OF_MEMORY) return ORPHEUS_ERR_OUT_OF_MEMORY;
    if(error==MA_DOES_NOT_EXIST) return ORPHEUS_ERR_NOT_FOUND;
    return ORPHEUS_ERR_PROCESSING;
}

int orpheus_builtin_mp3_in_prepare(orpheus_builtin_mp3_in_State* s,const OrpheusConfig* config) {
    s->total_frames=0;
    if(!s->file_path[0]) return ORPHEUS_OK;
    if(!config->sample_rate) return ORPHEUS_ERR_INVALID_ARG;
    ma_decoder_config settings=ma_decoder_config_init(ma_format_f32,(ma_uint32)s->channels,config->sample_rate);
    ma_decoder decoder;
    ma_result result;
#ifdef _WIN32
    int length=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s->file_path,-1,NULL,0);
    if(length<=0) return ORPHEUS_ERR_INVALID_ARG;
    wchar_t* wide=malloc((size_t)length*sizeof(*wide));
    if(!wide) return ORPHEUS_ERR_OUT_OF_MEMORY;
    if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s->file_path,-1,wide,length)) {
        free(wide);return ORPHEUS_ERR_INVALID_ARG;
    }
    result=ma_decoder_init_file_w(wide,&settings,&decoder);
    free(wide);
#else
    result=ma_decoder_init_file(s->file_path,&settings,&decoder);
#endif
    if(result!=MA_SUCCESS) return decoder_error(result);
    size_t count=(size_t)MP3_IN_MAX_FRAMES*(uint32_t)s->channels;
    if(count>SIZE_MAX/sizeof(float)) {
        ma_decoder_uninit(&decoder);return ORPHEUS_ERR_INVALID_ARG;
    }
    float* samples=malloc(count*sizeof(float));
    if(!samples) {
        ma_decoder_uninit(&decoder);return ORPHEUS_ERR_OUT_OF_MEMORY;
    }
    uint32_t total=0;
    int rc=ORPHEUS_OK;
    for(;;) {
        ma_uint64 got=0;
        uint32_t want=MP3_IN_MAX_FRAMES-total;
        if(!want) {
            float probe[32];
            result=ma_decoder_read_pcm_frames(&decoder,probe,1,&got);
            if(result!=MA_SUCCESS && result!=MA_AT_END) rc=decoder_error(result);
            else if(got) rc=ORPHEUS_ERR_INVALID_ARG;
            break;
        }
        if(want>4096) want=4096;
        result=ma_decoder_read_pcm_frames(&decoder,samples+(size_t)total*s->channels,want,&got);
        if(result!=MA_SUCCESS && result!=MA_AT_END) { rc=decoder_error(result);break; }
        if(got>want) { rc=ORPHEUS_ERR_PROCESSING;break; }
        total+=(uint32_t)got;
        if(result==MA_AT_END || !got) break;
    }
    ma_decoder_uninit(&decoder);
    if(rc) { free(samples);return rc; }
    s->algorithm.samples=samples;s->total_frames=(int32_t)total;
    return ORPHEUS_OK;
}

int orpheus_builtin_mp3_in_reset(orpheus_builtin_mp3_in_State* s) {
    s->algorithm.position=0;
    return ORPHEUS_OK;
}

int orpheus_builtin_mp3_in_destroy(orpheus_builtin_mp3_in_State* s) {
    free(s->algorithm.samples);s->algorithm.samples=NULL;
    return ORPHEUS_OK;
}

static bool overlap(const void* a,size_t na,const void* b,size_t nb) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return na && nb && (x>=y ? x-y<nb : y-x<na);
}

int orpheus_builtin_mp3_in_algorithm(orpheus_builtin_mp3_in_State* s,
    orpheus_builtin_mp3_in_IO* io,const OrpheusProcessContext* ctx) {
    if(!ctx->frame_count) return ORPHEUS_OK;
    if(!io->out.buffer || s->total_frames<0 || s->algorithm.position>(uint32_t)s->total_frames ||
        (s->total_frames && !s->algorithm.samples)) return ORPHEUS_ERR_INVALID_ARG;
    size_t bytes=(size_t)ctx->frame_count*s->channels*sizeof(float);
    if(overlap(io->out.samples,bytes,s,sizeof(*s)) || (s->algorithm.samples &&
        overlap(io->out.samples,bytes,s->algorithm.samples,(size_t)MP3_IN_MAX_FRAMES*s->channels*sizeof(float))))
        return ORPHEUS_ERR_INVALID_ARG;
    uint32_t remaining=(uint32_t)s->total_frames-s->algorithm.position;
    uint32_t frames=ctx->frame_count<remaining ? ctx->frame_count : remaining;
    size_t copied=(size_t)frames*s->channels*sizeof(float);
    if(copied) memcpy(io->out.samples,s->algorithm.samples+(size_t)s->algorithm.position*s->channels,copied);
    if(bytes>copied) memset((uint8_t*)io->out.samples+copied,0,bytes-copied);
    s->algorithm.position+=frames;io->out.buffer->frame_count=ctx->frame_count;
    int rc=orpheus_builtin_mp3_in_emit_out(ctx,&io->out);
    return rc ? rc : orpheus_builtin_mp3_in_emit_tick(ctx,&io->tick);
}
