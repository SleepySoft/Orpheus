#include "component_state.h"

#include <math.h>
#include <string.h>

#define FRAMES 128
#define CHANNELS 2
typedef orpheus_builtin_loudness_normalizer_State LnState;

static int fail_delivery(void* user,uint32_t port,const OrpheusDelivery* delivery) {
    unsigned* calls=(unsigned*)user;
    (*calls)++;
    return port==1 && delivery && delivery->buffer && delivery->buffer->frame_count==2 ?
        ORPHEUS_ERR_PROCESSING : ORPHEUS_ERR_INVALID_ARG;
}

static int verify_contract(const OrpheusComponentInterface* iface,LnState* state) {
    float input[5]={.25f,.5f,-.5f,-.25f,1},output[4]={0};
    OrpheusBuffer in={input,ORPHEUS_FORMAT_F32,2,2,2,true};
    OrpheusBuffer out={output,ORPHEUS_FORMAT_F32,2,2,2,true};
    const OrpheusBuffer* inputs[]={&in};OrpheusBuffer* outputs[]={&out};
    OrpheusProcessContext ctx={.inputs=inputs,.outputs=outputs,.input_count=1,.output_count=1,
        .frame_count=0,.sample_rate=48000};
    if(iface->reset(state)) return 1;
    LnState before=*state;
    if(iface->process(state,&ctx) || out.frame_count || memcmp(&before,state,sizeof(before))) return 2;
    ctx.frame_count=2;out.data=input+1;
    if(iface->process(state,&ctx)!=ORPHEUS_ERR_INVALID_ARG || memcmp(&before,state,sizeof(before))) return 3;
    out.data=output;in.frame_count=1;
    if(iface->process(state,&ctx)!=ORPHEUS_ERR_INVALID_ARG || memcmp(&before,state,sizeof(before))) return 4;
    in.frame_count=2;out.data=input;
    if(iface->process(state,&ctx) || state->algorithm.detector_energy<=0) return 5;
    const char* probes[]={"input_db","gain_db","output_db"};
    OrpheusValue value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=0};
    for(unsigned n=0;n<3;n++) if(iface->set_parameter(state,probes[n],&value)!=ORPHEUS_ERR_UNSUPPORTED) return 6;
    value.value.f32=NAN;
    if(iface->set_parameter(state,"target_db",&value)!=ORPHEUS_ERR_INVALID_ARG) return 7;
    if(iface->reset(state)) return 8;
    unsigned calls=0;
    OrpheusContinuation continuation={.user=&calls,.emit=fail_delivery};
    ctx.continuation=&continuation;
    if(iface->process(state,&ctx)!=ORPHEUS_ERR_PROCESSING || calls!=1 || state->algorithm.detector_energy<=0) return 9;
    ctx.continuation=NULL;out.data=output;
    if(iface->reset(state)) return 10;
    input[0]=NAN;input[1]=INFINITY;input[2]=-INFINITY;input[3]=-0.0f;
    if(iface->process(state,&ctx) || !isnan(state->algorithm.detector_energy) ||
       !isnan(state->input_db) || state->gain_db!=0 || state->output_db!=-120 ||
       !isnan(output[0]) || output[1]!=INFINITY || output[2]!=-INFINITY || !signbit(output[3])) return 11;
    return 0;
}

static int read_probe(const OrpheusComponentInterface* iface, void* state,
                      const char* id, float* result) {
    OrpheusValue value;
    if (iface->get_parameter(state, id, &value) != ORPHEUS_OK) return -1;
    if (value.type != ORPHEUS_VALUE_FLOAT) return -2;
    *result = value.value.f32;
    return 0;
}

static int process_blocks(const OrpheusComponentInterface* iface, void* state,
                          float amplitude, int blocks, float* output) {
    float input[FRAMES * CHANNELS];
    for (size_t i = 0; i < FRAMES * CHANNELS; ++i) input[i] = amplitude;

    OrpheusBuffer input_buffer = {
        input, ORPHEUS_FORMAT_F32, CHANNELS, FRAMES, FRAMES, true
    };
    OrpheusBuffer output_buffer = {
        output, ORPHEUS_FORMAT_F32, CHANNELS, FRAMES, FRAMES, true
    };
    const OrpheusBuffer* inputs[] = {&input_buffer};
    OrpheusBuffer* outputs[] = {&output_buffer};
    OrpheusProcessContext context = {
        .state = state,
        .inputs = inputs,
        .outputs = outputs,
        .input_count = 1,
        .output_count = 1,
        .frame_count = FRAMES,
        .sample_rate = 48000,
        .valid_frames = FRAMES,
    };
    for (int block = 0; block < blocks; ++block) {
        if (iface->process(state, &context) != ORPHEUS_OK) return -1;
    }
    return 0;
}

int main(void) {
    const OrpheusComponentInterface* iface = orpheus_get_interface();
    if (iface == NULL) return 1;

    const char* ids[] = {
        "target_db", "min_gain_db", "max_gain_db", "gate_db",
        "detector_attack_ms", "detector_release_ms",
        "gain_attack_ms", "gain_release_ms", "channels"
    };
    OrpheusValue values[9];
    memset(values, 0, sizeof(values));
    for (int i = 0; i < 8; ++i) values[i].type = ORPHEUS_VALUE_FLOAT;
    values[0].value.f32 = -18.0f;
    values[1].value.f32 = -12.0f;
    values[2].value.f32 = 12.0f;
    values[3].value.f32 = -55.0f;
    values[4].value.f32 = 5.0f;
    values[5].value.f32 = 20.0f;
    values[6].value.f32 = 5.0f;
    values[7].value.f32 = 50.0f;
    values[8].type = ORPHEUS_VALUE_INT;
    values[8].value.i32 = CHANNELS;

    orpheus_builtin_loudness_normalizer_State state_memory;
    memset(&state_memory, 0, sizeof(state_memory));
    OrpheusConfig config = {
        .sample_rate = 48000,
        .block_size = FRAMES,
        .channels = CHANNELS,
        .param_ids = ids,
        .param_values = values,
        .param_count = 9,
        .state_block = &state_memory,
    };
    void* state = NULL;
    if (iface->create(&state, &config) != ORPHEUS_OK) return 2;
    if (iface->prepare(state, &config) != ORPHEUS_OK) return 3;

    float output[FRAMES * CHANNELS];
    float gain_db = 0.0f;
    float output_db = 0.0f;

    if (process_blocks(iface, state, 0.05f, 300, output) != 0) return 4;
    if (read_probe(iface, state, "gain_db", &gain_db) != 0) return 5;
    if (read_probe(iface, state, "output_db", &output_db) != 0) return 6;
    if (gain_db < 7.8f || gain_db > 8.3f) return 7;
    if (output_db < -18.2f || output_db > -17.8f) return 8;

    if (process_blocks(iface, state, 0.5f, 300, output) != 0) return 9;
    if (read_probe(iface, state, "gain_db", &gain_db) != 0) return 10;
    if (read_probe(iface, state, "output_db", &output_db) != 0) return 11;
    if (gain_db < -12.1f || gain_db > -11.7f) return 12;
    if (output_db < -18.2f || output_db > -17.7f) return 13;

    if (iface->reset(state) != ORPHEUS_OK) return 14;
    if (process_blocks(iface, state, 0.0f, 100, output) != 0) return 15;
    if (read_probe(iface, state, "gain_db", &gain_db) != 0) return 16;
    if (fabsf(gain_db) > 0.001f) return 17;
    for (size_t i = 0; i < FRAMES * CHANNELS; ++i) {
        if (output[i] != 0.0f) return 18;
    }
    if (verify_contract(iface,state) != 0) return 19;
    if (iface->destroy(state) != ORPHEUS_OK) return 20;
    return 0;
}
