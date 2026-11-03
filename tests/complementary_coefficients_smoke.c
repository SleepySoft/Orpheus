#include "orpheus_complementary_coefficients.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%d: %s\n",__LINE__,#x); return 1; } } while (0)
int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    const uint32_t bits[]={0,0x80000000u,0x7f800000u,0xff800000u,0x7fc12345u,0x7f812345u,0x3f123456u,0xc0400000u};
    float input[8],output[16],expected[16]; memcpy(input,bits,sizeof(input));
    const char* ids[]={"channels","elements","total"};
    OrpheusValue params[]={{.type=ORPHEUS_VALUE_INT,.value.i32=2},
                          {.type=ORPHEUS_VALUE_INT,.value.i32=4},
                          {.type=ORPHEUS_VALUE_FLOAT,.value.f32=1.0f}};
    OrpheusConfig config={.sample_rate=48000,.block_size=1,.param_ids=ids,.param_values=params,.param_count=3};
    OrpheusBuffer in={.data=input,.channels=2,.frame_count=4,.frame_capacity=4,.format=ORPHEUS_FORMAT_F32,.interleaved=true};
    OrpheusBuffer out={.data=output,.channels=4,.frame_capacity=4,.format=ORPHEUS_FORMAT_F32,.interleaved=true};
    const OrpheusBuffer* inputs[]={&in}; OrpheusBuffer* outputs[]={&out};
    OrpheusProcessContext context={.inputs=inputs,.outputs=outputs,.input_count=1,.output_count=1,.frame_count=1};
    ComplementaryCoefficientsState state={0};
    for (unsigned kind=0;kind<2;++kind) {
        float total=kind ? -0.5f : 1.0f; params[2].value.f32=total;
        CHECK(api->prepare(&state,&config)==ORPHEUS_OK);
        for (unsigned row=0;row<4;++row) for (unsigned c=0;c<2;++c) {
            memcpy(&expected[row*4+c],&input[row*2+c],4);
            expected[row*4+2+c]=total-input[row*2+c];
        }
        CHECK(api->process(&state,&context)==ORPHEUS_OK && out.frame_count==4);
        CHECK(!memcmp(output,expected,sizeof(output)));
        float before[16]; memcpy(before,output,sizeof(output));
        --in.frame_count; CHECK(api->process(&state,&context)==ORPHEUS_ERR_INVALID_ARG); ++in.frame_count;
        out.frame_capacity=3; CHECK(api->process(&state,&context)==ORPHEUS_ERR_INVALID_ARG); out.frame_capacity=4;
        out.channels=3; CHECK(api->process(&state,&context)==ORPHEUS_ERR_INVALID_ARG); out.channels=4;
        in.interleaved=false; CHECK(api->process(&state,&context)==ORPHEUS_ERR_INVALID_ARG); in.interleaved=true;
        CHECK(!memcmp(output,before,sizeof(output)));
        out.data=input; CHECK(api->process(&state,&context)==ORPHEUS_ERR_INVALID_ARG); out.data=output;
        outputs[0]=NULL; CHECK(api->process(&state,&context)==ORPHEUS_OK); outputs[0]=&out;
        context.frame_count=0; CHECK(api->process(&state,&context)==ORPHEUS_OK); context.frame_count=1;
        CHECK(api->reset(&state)==ORPHEUS_OK);
        CHECK(api->process(&state,&context)==ORPHEUS_OK && !memcmp(output,expected,sizeof(output)));
    }
    params[2].value.f32=NAN; CHECK(api->prepare(&state,&config)==ORPHEUS_ERR_INVALID_ARG);
    params[2].value.f32=1.0f; params[0].value.i32=0;
    CHECK(api->prepare(&state,&config)==ORPHEUS_ERR_INVALID_ARG);
    params[0].value.i32=33; CHECK(api->prepare(&state,&config)==ORPHEUS_ERR_INVALID_ARG);
    config.param_count=0; config.state_block=&state;
    void* allocated=NULL;
    CHECK(api->create(&allocated,&config)==ORPHEUS_OK && allocated==&state && !state.owns_memory);
    CHECK(api->prepare(allocated,&config)==ORPHEUS_OK && api->destroy(allocated)==ORPHEUS_OK);
    config.state_block=NULL;
    CHECK(api->create(&allocated,&config)==ORPHEUS_OK && ((ComplementaryCoefficientsState*)allocated)->owns_memory);
    CHECK(api->destroy(allocated)==ORPHEUS_OK);
    return 0;
}
