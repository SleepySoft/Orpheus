#include "component_state.h"
#ifdef malloc
#undef malloc
#endif
#ifdef realloc
#undef realloc
#endif
#ifdef free
#undef free
#endif
#define MA_NO_DEVICE_IO
#define MA_NO_ENGINE
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENCODING
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr) do { if(!(expr)) { fprintf(stderr,"%d: %s\n",__LINE__,#expr);return 1; } } while(0)
typedef orpheus_builtin_mp3_in_State State;
static unsigned heap_calls;
static int fail_malloc;
void* mp3_test_malloc(size_t size) { ++heap_calls;return fail_malloc ? NULL : malloc(size); }
void* mp3_test_realloc(void* ptr,size_t size) { ++heap_calls;return realloc(ptr,size); }
void mp3_test_free(void* ptr) { ++heap_calls;free(ptr); }

typedef struct { unsigned sequence;int error;uint32_t frames; } Delivery;
static int emit(void* user,uint32_t port,const OrpheusDelivery* delivery) {
    Delivery* result=user;
    if(!delivery || delivery->frame_index!=333 || delivery->epoch!=8 || port<1 || port>2 ||
        (port==1 && (!delivery->buffer || delivery->buffer->frame_count!=result->frames)) ||
        (port==2 && delivery->buffer)) return ORPHEUS_ERR_INVALID_ARG;
    result->sequence=result->sequence*10+port;
    return port==1 ? result->error : 0;
}

static int source_and_errors(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    State state;
    const char* ids[]={"file_path"};
    OrpheusValue path={.type=ORPHEUS_VALUE_STRING,.value.str=MP3_FIXTURE};
    OrpheusConfig config={.channels=2,.sample_rate=32000,.param_count=1,.param_ids=ids,
        .param_values=&path,.state_block=&state};
    void* handle=NULL;
    CHECK(api->create(&handle,&config)==0);
    fail_malloc=1;
    CHECK(api->prepare(handle,&config)==ORPHEUS_ERR_OUT_OF_MEMORY && !state.algorithm.samples);
    fail_malloc=0;
    CHECK(api->prepare(handle,&config)==0 && state.total_frames>3);
    ma_decoder_config settings=ma_decoder_config_init(ma_format_f32,2,32000);
    float* reference=NULL;ma_uint64 total=0;
    CHECK(ma_decode_file(MP3_FIXTURE,&settings,&total,(void**)&reference)==MA_SUCCESS &&
        total==(ma_uint64)state.total_frames);
    float data[16];
    OrpheusBuffer out={.data=data,.format=ORPHEUS_FORMAT_F32,.channels=2,.interleaved=true,
        .frame_count=5,.frame_capacity=8};
    OrpheusBuffer* outputs[]={&out};
    Delivery delivery={.error=-77,.frames=5};
    OrpheusContinuation continuation={.user=&delivery,.emit=emit};
    OrpheusProcessContext ctx={.outputs=outputs,.output_count=1,.frame_count=5,.valid_frames=5,
        .frame_index=333,.epoch=8,.continuation=&continuation};
    unsigned before=heap_calls;
    CHECK(api->process(handle,&ctx)==-77 && heap_calls==before && delivery.sequence==1 &&
        state.algorithm.position==5 && !memcmp(data,reference,10*sizeof(float)));
    delivery.error=0;delivery.sequence=0;delivery.frames=2;ctx.frame_count=2;
    CHECK(api->process(handle,&ctx)==0 && heap_calls==before && delivery.sequence==12 &&
        state.algorithm.position==7 && !memcmp(data,reference+10,4*sizeof(float)));
    State saved=state;
    outputs[0]=NULL;CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);outputs[0]=&out;
    out.frame_count=1;out.frame_capacity=1;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);out.frame_count=2;out.frame_capacity=8;
    void* aliases[]={&state,state.algorithm.samples,state.algorithm.samples+1};
    for(unsigned i=0;i<3;i++) {
        out.data=aliases[i];CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);
    }
    out.data=data;
    state.algorithm.position=(uint32_t)state.total_frames+1;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);state.algorithm.position=saved.algorithm.position;
    CHECK(!memcmp(&saved,&state,sizeof(state)) && delivery.sequence==12 && heap_calls==before);
    ctx.frame_count=0;outputs[0]=NULL;
    CHECK(api->process(handle,&ctx)==0 && !memcmp(&saved,&state,sizeof(state)));
    outputs[0]=&out;ctx.frame_count=5;delivery.frames=5;delivery.sequence=0;
    state.algorithm.position=(uint32_t)state.total_frames-3;
    CHECK(api->process(handle,&ctx)==0 && delivery.sequence==12 && heap_calls==before &&
        !memcmp(data,reference+(total-3)*2,6*sizeof(float)));
    for(unsigned i=6;i<10;i++) CHECK(data[i]==0);
    delivery.sequence=0;
    CHECK(api->process(handle,&ctx)==0 && delivery.sequence==12 && heap_calls==before &&
        state.algorithm.position==(uint32_t)state.total_frames);
    for(unsigned i=0;i<10;i++) CHECK(data[i]==0);
    CHECK(api->reset(handle)==0 && !state.algorithm.position && state.total_frames==(int32_t)total);
    CHECK(api->process(handle,&ctx)==0 && !memcmp(data,reference,10*sizeof(float)));
    ma_free(reference,NULL);
    CHECK(api->destroy(handle)==0 && api->destroy(handle)==0);
    path.value.str="mp3_in_fixture_does_not_exist.mp3";
    CHECK(api->create(&handle,&config)==0);
    CHECK(api->prepare(handle,&config)!=0 && !state.algorithm.samples);
    path.value.str="";
    CHECK(api->prepare(handle,&config)==0 && state.total_frames==0);
    delivery.sequence=0;
    CHECK(api->process(handle,&ctx)==0 && delivery.sequence==12);
    for(unsigned i=0;i<10;i++) CHECK(data[i]==0);
    FILE* invalid=fopen("mp3_in_invalid_smoke.mp3","wb");
    CHECK(invalid && fwrite("invalid",1,7,invalid)==7 && fclose(invalid)==0);
    path.value.str="mp3_in_invalid_smoke.mp3";
    CHECK(api->prepare(handle,&config)==ORPHEUS_ERR_PROCESSING && !state.algorithm.samples &&
        remove("mp3_in_invalid_smoke.mp3")==0);
    path.value.str=MP3_FIXTURE;
    config.sample_rate=0;
    CHECK(api->prepare(handle,&config)==ORPHEUS_ERR_INVALID_ARG && !state.algorithm.samples);
    config.sample_rate=32000;
    path.value.str="";
    CHECK(api->prepare(handle,&config)==0 && state.total_frames==0);
    return api->destroy(handle);
}

static int capacity_limit(void) {
    const char* filename="mp3_in_capacity_smoke.wav";
    const OrpheusComponentInterface* api=orpheus_get_interface();
    for(unsigned extra=0;extra<2;extra++) {
        uint32_t samples=16u*1024u*1024u+extra;
        uint32_t header[]={0x46464952,samples*4+36,0x45564157,0x20746d66,16,
            0x00010003,48000,192000,0x00200004,0x61746164,samples*4};
        FILE* file=fopen(filename,"wb");
        CHECK(file && fwrite(header,sizeof(header),1,file)==1 &&
            fseek(file,(long)samples*4+43,SEEK_SET)==0 && fputc(0,file)!=EOF && fclose(file)==0);
        State state;
        void* handle=NULL;
        const char* key="file_path";
        OrpheusValue path={.type=ORPHEUS_VALUE_STRING,.value.str=filename};
        OrpheusConfig config={.channels=1,.sample_rate=48000,.param_count=1,.param_ids=&key,
            .param_values=&path,.state_block=&state};
        CHECK(api->create(&handle,&config)==0);
        int rc=api->prepare(handle,&config);
        CHECK(extra ? rc==ORPHEUS_ERR_INVALID_ARG && !state.algorithm.samples :
            rc==0 && state.total_frames==(int32_t)samples);
        CHECK(api->destroy(handle)==0 && remove(filename)==0);
    }
    return 0;
}

int main(int argc,char** argv) {
    if(argc==6 && !strcmp(argv[1],"--decode")) {
        ma_decoder_config config=ma_decoder_config_init(ma_format_f32,(ma_uint32)atoi(argv[3]),(ma_uint32)atoi(argv[4]));
        float* samples=NULL;ma_uint64 frames=0;
        CHECK(ma_decode_file(argv[2],&config,&frames,(void**)&samples)==MA_SUCCESS);
        FILE* output=fopen(argv[5],"wb");
        size_t count=(size_t)frames*config.channels;
        CHECK(output && fwrite(samples,sizeof(float),count,output)==count && fclose(output)==0);
        ma_free(samples,NULL);
        return 0;
    }
    CHECK(source_and_errors()==0);
    CHECK(capacity_limit()==0);
    return 0;
}
