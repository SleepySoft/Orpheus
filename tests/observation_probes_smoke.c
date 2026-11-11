#include "orpheus/builtin/probe_waveform/generated/component_state.h"
#include "orpheus/builtin/probe_spectrum/generated/component_state.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const OrpheusComponentInterface* test_probe_waveform(void);
const OrpheusComponentInterface* test_probe_spectrum(void);
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%d: %s\n", __LINE__, #x); return 1; } } while (0)
typedef struct Capture {
    OrpheusBuffer* output;
    const float* probe;
    float seen;
    unsigned calls;
    int error;
} Capture;
static OrpheusSlotInfo probe_slot;
static OrpheusSlotId next_slot;
static OrpheusSlotId add(void* user, const OrpheusSlotInfo* info) {
    (void)user;
    if (info->kind == ORPHEUS_SLOT_PROBE) probe_slot=*info;
    return ++next_slot;
}
static int emit(void* user, uint32_t port, const OrpheusDelivery* delivery) {
    Capture* capture=user;
    if (port != 1 || !delivery || delivery->buffer != capture->output) return ORPHEUS_ERR_INVALID_ARG;
    capture->seen=capture->probe[0]; capture->calls++;
    return capture->error;
}
static int check_slot(const OrpheusComponentInterface* api, void* state, const float* values, const char* key) {
    OrpheusRegistry registry={.add=add};
    CHECK(api->register_slots(state, &registry) == 0 && !strcmp(probe_slot.key, key)
        && probe_slot.type == ORPHEUS_VALUE_FLOAT && probe_slot.count == 1023
        && probe_slot.size == sizeof(float) && (probe_slot.flags & ORPHEUS_SLOT_READBACK)
        && !(probe_slot.flags & ORPHEUS_SLOT_DIRECT_WRITE)
        && (char*)state+probe_slot.offset == (const char*)values);
    OrpheusValue write={.type=ORPHEUS_VALUE_FLOAT, .value.f32=1};
    CHECK(api->set_parameter(state, key, &write) != ORPHEUS_OK);
    return 0;
}

static int waveform(unsigned channels, unsigned frames) {
    const OrpheusComponentInterface* api=test_probe_waveform();
    orpheus_builtin_probe_waveform_State state;
    const char* ids[]={"channels"};
    OrpheusValue params[]={{.type=ORPHEUS_VALUE_INT, .value.i32=channels}};
    OrpheusConfig config={.sample_rate=48000, .block_size=frames, .state_block=&state,
        .param_ids=ids, .param_values=params, .param_count=1};
    void* handle=NULL;
    CHECK(api->create(&handle, &config) == 0 && api->prepare(handle, &config) == 0);
    CHECK(check_slot(api, handle, state.waveform, "waveform") == 0);
    size_t count=(size_t)channels*frames;
    float* original=malloc(count*sizeof(float)); float* input=malloc((count+channels)*sizeof(float));
    float* output=malloc(count*sizeof(float)); CHECK(original && input && output);
    OrpheusBuffer in={.data=input, .channels=channels, .frame_count=frames, .frame_capacity=frames,
        .format=ORPHEUS_FORMAT_F32, .interleaved=true};
    OrpheusBuffer out=in; out.data=output;
    const OrpheusBuffer* inputs[]={&in}; OrpheusBuffer* outputs[]={&out};
    Capture capture={.output=&out, .probe=state.waveform};
    OrpheusContinuation continuation={.emit=emit, .user=&capture};
    OrpheusProcessContext ctx={.inputs=inputs, .outputs=outputs, .input_count=1, .output_count=1,
        .frame_count=frames, .continuation=&continuation};
    float expected[1023]={0}; unsigned head=0;
    for (unsigned pass=0; pass<7; pass++) {
        for (size_t i=0; i<count; i++) {
            uint32_t bits[] = {0, 0x80000000, 0x7f800000, 0xff800000, 0x7fc12345, 0x7f812345, 1};
            uint32_t value=i%19<7 ? bits[i%19] : 0x3f000000 | ((uint32_t)(i+pass*1000003)*2654435761u & 0x7fffff);
            memcpy(original+i, &value, sizeof(value));
        }
        memcpy(input, original, count*sizeof(float));
        out.data=pass==1 ? input : pass==2 ? input+channels : output;
        if (pass==3) { outputs[0]=NULL; capture.output=NULL; } else { outputs[0]=&out; capture.output=&out; }
        for (unsigned f=0; f<frames; f++) {
            memcpy(expected+head, original+(size_t)f*channels, sizeof(float)); head=(head+1)%1023;
        }
        capture.calls=0; capture.error=pass==4 ? -77 : 0;
        CHECK(api->process(handle, &ctx) == capture.error && capture.calls == 1
            && state.algorithm.head == head && !memcmp(expected, state.waveform, sizeof(expected)));
        CHECK(!memcmp(&capture.seen, expected, sizeof(float)));
        if (outputs[0]) CHECK(out.frame_count == frames && !memcmp(out.data, original, count*sizeof(float)));
        CHECK(api->reset(handle) == 0 && state.algorithm.head == head
            && !memcmp(expected, state.waveform, sizeof(expected)));
    }
    outputs[0]=&out; out.data=output; capture.output=&out; capture.calls=0; capture.error=0;
    in.frame_count=frames-1;
    CHECK(api->process(handle, &ctx) == ORPHEUS_ERR_INVALID_ARG && !capture.calls);
    in.frame_count=frames; out.frame_capacity=frames-1;
    CHECK(api->process(handle, &ctx) == ORPHEUS_ERR_INVALID_ARG && !capture.calls);
    out.frame_capacity=frames; ctx.frame_count=0;
    CHECK(api->process(handle, &ctx) == 0 && !capture.calls
        && !memcmp(expected, state.waveform, sizeof(expected)) && state.algorithm.head == head);
    params[0].value.i32=33;
    CHECK(api->prepare(handle, &config) == ORPHEUS_ERR_INVALID_ARG && state.algorithm.head == head);
    params[0].value.i32=channels;
    CHECK(api->prepare(handle, &config) == 0 && !state.algorithm.head);
    for (unsigned i=0; i<1023; i++) CHECK(state.waveform[i] == 0);
    CHECK(api->destroy(handle) == 0);
    config.state_block=NULL; CHECK(api->create(&handle, &config) == ORPHEUS_ERR_INVALID_ARG);
    free(original); free(input); free(output);
    return 0;
}

static int spectrum(unsigned channels, unsigned requested, bool sinusoid) {
    const OrpheusComponentInterface* api=test_probe_spectrum();
    orpheus_builtin_probe_spectrum_State state;
    const char* ids[]={"channels", "window_size"};
    OrpheusValue params[]={{.type=ORPHEUS_VALUE_INT, .value.i32=channels},
        {.type=ORPHEUS_VALUE_INT, .value.i32=requested}};
    OrpheusConfig config={.sample_rate=48000, .block_size=17, .state_block=&state,
        .param_ids=ids, .param_values=params, .param_count=2};
    void* handle=NULL;
    CHECK(api->create(&handle, &config) == 0 && api->prepare(handle, &config) == 0);
    unsigned n=64; while(n<requested) n*=2;
    CHECK(state.window_size == (int32_t)n && state.algorithm.size == n);
    CHECK(check_slot(api, handle, state.spectrum, "spectrum") == 0);
    unsigned capacity=2*n+17;
    size_t count=(size_t)channels*capacity;
    float* original=malloc(count*sizeof(float)); float* input=malloc((count+channels)*sizeof(float));
    float* output=malloc(count*sizeof(float)); CHECK(original && input && output);
    float* ring=calloc(n,sizeof(float)); CHECK(ring);
    double expected[1023]={0}, magnitudes[2048];
    unsigned position=0, seen=0; uint64_t cursor=0;
    OrpheusBuffer in={.data=input, .channels=channels, .frame_count=capacity, .frame_capacity=capacity,
        .format=ORPHEUS_FORMAT_F32, .interleaved=true};
    OrpheusBuffer out=in; out.data=output;
    const OrpheusBuffer* inputs[]={&in}; OrpheusBuffer* outputs[]={&out};
    Capture capture={.output=&out, .probe=state.spectrum};
    OrpheusContinuation continuation={.emit=emit, .user=&capture};
    OrpheusProcessContext ctx={.inputs=inputs, .outputs=outputs, .input_count=1, .output_count=1,
        .continuation=&continuation};
    const unsigned steps[]={1, n/2, n-1, 2*n+17, n/2, n};
    for (unsigned pass=0; pass<6; pass++) {
        unsigned frames=steps[pass];
        for (unsigned f=0; f<frames; f++) for (unsigned c=0; c<channels; c++)
            original[(size_t)f*channels+c] = c ? (float)(f+c+pass)
                : sinusoid ? (float)(.25+.5*cos(6.283185307179586*3*(cursor+f)/n))
                : (cursor+f)%n == n/2 ? (float)((cursor+f)/n+1)*.25f : 0;
        memcpy(input, original, (size_t)frames*channels*sizeof(float));
        for (unsigned f=0; f<frames; f++) {
            ring[position]=original[(size_t)f*channels]; position=(position+1)%n;
            if (++seen == n) {
                seen=0;
                if (sinusoid) {
                    for (unsigned k=0; k<n/2; k++) {
                        double re=0, im=0;
                        for (unsigned i=0; i<n; i++) {
                            double angle=6.283185307179586*k*i/n;
                            double value=ring[i]*(.5-.5*cos(6.283185307179586*i/(n-1)));
                            re+=value*cos(angle); im-=value*sin(angle);
                        }
                        magnitudes[k]=2*sqrt(re*re+im*im)/n;
                    }
                } else {
                    double weight=.5-.5*cos(6.283185307179586*(n/2)/(n-1));
                    for (unsigned k=0; k<n/2; k++) magnitudes[k]=2*ring[n/2]*weight/n;
                }
                for (unsigned i=0; i<1023; i++) {
                    double source=(double)i*(n/2-1)/1022;
                    unsigned lower=(unsigned)source, upper=lower+1<n/2 ? lower+1 : lower;
                    expected[i]=magnitudes[lower]+(magnitudes[upper]-magnitudes[lower])*(source-lower);
                }
            }
        }
        out.data=pass==1 ? input : pass==2 ? input+channels : output;
        outputs[0]=pass==3 ? NULL : &out; capture.output=outputs[0];
        ctx.frame_count=in.frame_count=frames;
        capture.calls=0; capture.error=pass==4 ? -77 : 0;
        CHECK(api->process(handle, &ctx) == capture.error && capture.calls == 1
            && state.algorithm.ring_pos == position && state.algorithm.samples_seen == seen);
        CHECK(!memcmp(state.algorithm.ring,ring,n*sizeof(float)));
        for (unsigned i=0; i<1023; i++) CHECK(fabs(state.spectrum[i]-expected[i])<0.00001);
        CHECK(fabs(capture.seen-expected[0])<0.00001);
        if (outputs[0]) CHECK(out.frame_count == frames
            && !memcmp(out.data,original,(size_t)frames*channels*sizeof(float)));
        cursor+=frames;
    }
    outputs[0]=&out; capture.output=&out; out.data=output; capture.calls=0; capture.error=0;
    float saved[1023]; memcpy(saved,state.spectrum,sizeof(saved));
    in.frame_count=ctx.frame_count-1;
    CHECK(api->process(handle,&ctx) == ORPHEUS_ERR_INVALID_ARG && !capture.calls); in.frame_count=ctx.frame_count;
    out.frame_capacity=ctx.frame_count-1;
    CHECK(api->process(handle,&ctx) == ORPHEUS_ERR_INVALID_ARG && !capture.calls); out.frame_capacity=capacity;
    ctx.frame_count=0;
    CHECK(api->process(handle,&ctx) == 0 && !capture.calls && !memcmp(saved,state.spectrum,sizeof(saved))
        && !memcmp(state.algorithm.ring,ring,n*sizeof(float)));
    float* old=state.algorithm.ring; params[1].value.i32=4097;
    CHECK(api->prepare(handle,&config) == ORPHEUS_ERR_INVALID_ARG && state.algorithm.ring == old
        && !memcmp(saved,state.spectrum,sizeof(saved)));
    params[1].value.i32=requested;
    CHECK(api->reset(handle) == 0 && !state.algorithm.ring_pos && !state.algorithm.samples_seen);
    for (unsigned i=0; i<n; i++) CHECK(state.algorithm.ring[i] == 0);
    for (unsigned i=0; i<n/2; i++) CHECK(state.algorithm.mags[i] == 0);
    for (unsigned i=0; i<1023; i++) CHECK(state.spectrum[i] == 0);
    CHECK(api->prepare(handle,&config) == 0 && api->destroy(handle) == 0
        && !state.algorithm.ring && !state.algorithm.window && !state.algorithm.re
        && !state.algorithm.im && !state.algorithm.mags);
    config.state_block=NULL; CHECK(api->create(&handle,&config) == ORPHEUS_ERR_INVALID_ARG);
    free(original); free(input); free(output); free(ring);
    return 0;
}

int main(void) {
    CHECK(waveform(1,1) == 0 && waveform(3,513) == 0 && waveform(32,4096) == 0);
    CHECK(spectrum(1,64,true) == 0 && spectrum(3,65,false) == 0 && spectrum(32,4096,false) == 0);
    return 0;
}
