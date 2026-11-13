#include "component_state.h"
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%d: %s\n", __LINE__, #x); return 1; } } while (0)
extern const OrpheusComponentInterface* orpheus_get_interface(void);

static int calls;
static uint32_t emitted_frames;
static int delivery_error = -77;
static int deliver(void* user, uint32_t port, const OrpheusDelivery* delivery) {
    (void)user;
    ++calls;
    emitted_frames = delivery && delivery->buffer ? delivery->buffer->frame_count : 99;
    return port == 1 && delivery && delivery->frame_index == 11 && delivery->epoch == 3 &&
        delivery->timeline_flags == ORPHEUS_TIMELINE_DISCONTINUITY ? delivery_error : -99;
}

int main(void) {
    const OrpheusComponentInterface* api = orpheus_get_interface();
    const OrpheusComponentDescriptor* desc = api->get_descriptor();
    CHECK(desc->port_count == 3 && desc->param_count == 3);
    CHECK(desc->ports[0].type == ORPHEUS_PORT_AUDIO && desc->ports[0].direction == ORPHEUS_PORT_INPUT &&
        !strcmp(desc->ports[0].id, "in0") && desc->ports[0].is_variable &&
        desc->ports[0].channels_param && !strcmp(desc->ports[0].channels_param, "channels"));
    CHECK(!strcmp(desc->ports[1].id, "in1") && !strcmp(desc->ports[2].id, "out") &&
        desc->ports[2].direction == ORPHEUS_PORT_OUTPUT);

    orpheus_builtin_rate_sync_State external;
    OrpheusValue channels = {.type = ORPHEUS_VALUE_INT, .value.i32 = 2};
    const char* ids[] = {"channels"};
    OrpheusConfig config = {.state_block = &external, .sample_rate = 48000, .block_size = 96,
        .channels = 1, .param_ids = ids, .param_values = &channels, .param_count = 1};
    void* state = NULL;
    CHECK(api->create(&state, &config) == 0 && api->prepare(state, &config) == 0);
    OrpheusValue back = {.type = ORPHEUS_VALUE_INT};
    CHECK(api->get_parameter(state, "channels", &back) == 0 && back.value.i32 == 2);
    CHECK(api->get_parameter(state, "mode", &back) == 0 && back.value.i32 == 0);
    CHECK(api->get_parameter(state, "buffer_length", &back) == 0 && back.value.i32 == 0);
    back.value.i32 = 4;
    CHECK(api->set_parameter(state, "channels", &back) == ORPHEUS_ERR_UNSUPPORTED);
    CHECK(api->get_parameter(state, "channels", &back) == 0 && back.value.i32 == 2);

    float a[96 * 2], b[96 * 2], out[192 * 2];
    for (int i = 0; i < 96 * 2; ++i) { a[i] = 0.25f; b[i] = -0.5f; }
    OrpheusBuffer in0 = {.format = ORPHEUS_FORMAT_F32, .channels = 2, .interleaved = true,
        .frame_capacity = 96, .frame_count = 96, .data = a};
    OrpheusBuffer in1 = {.format = ORPHEUS_FORMAT_F32, .channels = 2, .interleaved = true,
        .frame_capacity = 96, .frame_count = 96, .data = b};
    OrpheusBuffer outb = {.format = ORPHEUS_FORMAT_F32, .channels = 2, .interleaved = true,
        .frame_capacity = 192, .frame_count = 0, .data = out};
    const OrpheusBuffer* inputs[] = {&in0, &in1};
    OrpheusBuffer* outputs[] = {&outb};
    OrpheusContinuation continuation = {.emit = deliver};
    OrpheusProcessContext ctx = {.inputs = inputs, .outputs = outputs, .input_count = 2,
        .output_count = 1, .frame_count = 96, .sample_rate = 48000, .continuation = &continuation,
        .frame_index = 11, .epoch = 3, .timeline_flags = ORPHEUS_TIMELINE_DISCONTINUITY,
        .trigger_input = UINT32_MAX};

    /* 自动模式：首块即对齐，96帧求和交付 */
    CHECK(api->process(state, &ctx) == -77 && calls == 1 && emitted_frames == 96);
    for (int i = 0; i < 96 * 2; ++i) {
        float expected = -0.25f;
        CHECK(memcmp(out + i, &expected, 4) == 0);
    }

    /* 错误继续：容量不变量（帧数超过FIFO容量）报错且状态不损坏 */
    in0.frame_count = 193;
    ctx.frame_count = 193;
    CHECK(api->process(state, &ctx) == ORPHEUS_ERR_INVALID_ARG);
    in0.frame_count = 96;
    ctx.frame_count = 96;
    /* 别名输入/输出重叠明确拒绝 */
    outb.data = a;
    CHECK(api->process(state, &ctx) == ORPHEUS_ERR_INVALID_ARG);
    outb.data = out;
    CHECK(api->process(state, &ctx) == -77 && calls == 2 && emitted_frames == 96);
    /* 通道不匹配与缺输入缓冲由壳/算法拒绝 */
    in1.channels = 1;
    CHECK(api->process(state, &ctx) == ORPHEUS_ERR_INVALID_ARG);
    in1.channels = 2;
    /* 零帧上下文不交付 */
    ctx.frame_count = 0;
    CHECK(api->process(state, &ctx) == 0 && calls == 2);
    ctx.frame_count = 96;

    /* reset 清空缓冲：下一拍无足够数据则交付0帧 */
    CHECK(api->reset(state) == 0);
    delivery_error = 0;
    CHECK(api->process(state, &ctx) == 0 && calls == 3 && emitted_frames == 96);

    CHECK(api->destroy(state) == 0);
    CHECK(api->destroy(state) == 0);

    /* 固定模式：buffer_length=192>块长96，第一拍只缓冲交付0帧，之后每拍96帧 */
    {
        OrpheusValue fixed[] = {{.type = ORPHEUS_VALUE_INT, .value.i32 = 2},
            {.type = ORPHEUS_VALUE_INT, .value.i32 = 1},
            {.type = ORPHEUS_VALUE_INT, .value.i32 = 192}};
        const char* fids[] = {"channels", "mode", "buffer_length"};
        OrpheusConfig fconfig = {.state_block = &external, .sample_rate = 48000,
            .block_size = 96, .channels = 1, .param_ids = fids, .param_values = fixed,
            .param_count = 3};
        void* fstate = NULL;
        CHECK(api->create(&fstate, &fconfig) == 0 && api->prepare(fstate, &fconfig) == 0);
        outb.frame_capacity = 96; /* 真实 plan 中 out 容量=合流块长96 */
        calls = 0;
        CHECK(api->process(fstate, &ctx) == 0 && calls == 1 && emitted_frames == 0);
        CHECK(api->process(fstate, &ctx) == 0 && calls == 2 && emitted_frames == 96);
        CHECK(api->process(fstate, &ctx) == 0 && calls == 3 && emitted_frames == 96);
        CHECK(api->destroy(fstate) == 0);
    }

    /* 显式缓冲长度小于合流块长：准备期拒绝；实例不可运行但可安全销毁 */
    {
        OrpheusValue bad[] = {{.type = ORPHEUS_VALUE_INT, .value.i32 = 1},
            {.type = ORPHEUS_VALUE_INT, .value.i32 = 1},
            {.type = ORPHEUS_VALUE_INT, .value.i32 = 48}};
        const char* bids[] = {"channels", "mode", "buffer_length"};
        OrpheusConfig bconfig = {.state_block = &external, .sample_rate = 48000,
            .block_size = 96, .channels = 1, .param_ids = bids, .param_values = bad,
            .param_count = 3};
        void* bstate = NULL;
        CHECK(api->create(&bstate, &bconfig) == 0);
        CHECK(api->prepare(bstate, &bconfig) == ORPHEUS_ERR_INVALID_ARG);
        /* 拒绝后的实例不可运行（FIFO未建立），但可安全销毁 */
        OrpheusBuffer bin0 = {.format = ORPHEUS_FORMAT_F32, .channels = 1, .interleaved = true,
            .frame_capacity = 96, .frame_count = 96, .data = a};
        OrpheusBuffer bin1 = {.format = ORPHEUS_FORMAT_F32, .channels = 1, .interleaved = true,
            .frame_capacity = 96, .frame_count = 96, .data = b};
        OrpheusBuffer bout = {.format = ORPHEUS_FORMAT_F32, .channels = 1, .interleaved = true,
            .frame_capacity = 96, .frame_count = 0, .data = out};
        const OrpheusBuffer* binputs[] = {&bin0, &bin1};
        OrpheusBuffer* boutputs[] = {&bout};
        OrpheusProcessContext bctx = {.inputs = binputs, .outputs = boutputs, .input_count = 2,
            .output_count = 1, .frame_count = 96, .sample_rate = 48000,
            .frame_index = 11, .epoch = 3, .trigger_input = UINT32_MAX};
        CHECK(api->process(bstate, &bctx) == ORPHEUS_ERR_PROCESSING);
        CHECK(api->destroy(bstate) == 0);
        /* 提交前校验失败（越界）不触碰既有实例状态 */
        void* good = NULL;
        CHECK(api->create(&good, &config) == 0 && api->prepare(good, &config) == 0);
        orpheus_builtin_rate_sync_State snapshot = external;
        OrpheusValue oob = {.type = ORPHEUS_VALUE_INT, .value.i32 = 99};
        const char* oids[] = {"channels"};
        OrpheusConfig oconfig = {.state_block = &external, .sample_rate = 48000,
            .block_size = 96, .channels = 1, .param_ids = oids, .param_values = &oob,
            .param_count = 1};
        CHECK(api->prepare(good, &oconfig) == ORPHEUS_ERR_INVALID_ARG);
        CHECK(!memcmp(&external, &snapshot, sizeof(external)));
        CHECK(api->destroy(good) == 0);
    }

    /* 未显式声明channels时回退config->channels；状态块未对齐拒绝 */
    {
        OrpheusConfig cconfig = {.state_block = &external, .sample_rate = 48000,
            .block_size = 96, .channels = 3};
        void* cstate = NULL;
        CHECK(api->create(&cstate, &cconfig) == 0 && api->prepare(cstate, &cconfig) == 0);
        OrpheusValue c = {.type = ORPHEUS_VALUE_INT};
        CHECK(api->get_parameter(cstate, "channels", &c) == 0 && c.value.i32 == 3);
        CHECK(api->destroy(cstate) == 0);
        OrpheusConfig bad_align = {.state_block = (void*)((char*)&external + 1),
            .sample_rate = 48000, .block_size = 96, .channels = 1};
        void* misaligned = NULL;
        CHECK(api->create(&misaligned, &bad_align) == ORPHEUS_ERR_INVALID_ARG && !misaligned);
    }
    return 0;
}
