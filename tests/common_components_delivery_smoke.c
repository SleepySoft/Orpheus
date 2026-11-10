#include "orpheus_abi.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%d: %s\n", __LINE__, #x); return 1; } } while (0)
#define FLOAT(v) {.type=ORPHEUS_VALUE_FLOAT, .value.f32=(v)}

const OrpheusComponentInterface* delivery_balance(void);
const OrpheusComponentInterface* delivery_mute(void);
const OrpheusComponentInterface* delivery_square(void);
const OrpheusComponentInterface* delivery_saturation(void);
const OrpheusComponentInterface* delivery_soft_clipper(void);
const OrpheusComponentInterface* delivery_exponential_gain(void);

typedef struct {
    const OrpheusComponentInterface* api;
    void* state;
    float input[8], output[8];
    float delivered[8];
    OrpheusBuffer in, out;
    const OrpheusBuffer* inputs[1];
    OrpheusBuffer* outputs[1];
    OrpheusContinuation continuation;
    OrpheusProcessContext context;
    unsigned emits, slots;
    int emit_result;
    size_t probe_offset;
    bool probe_found, bad_delivery;
} Fixture;

static int emit(void* user, uint32_t port, const OrpheusDelivery* delivery) {
    Fixture* f = (Fixture*)user;
    ++f->emits;
    if (port != 1 || delivery->buffer != f->outputs[0] ||
        delivery->frame_index != 900 || delivery->epoch != 3 ||
        delivery->timeline_flags != ORPHEUS_TIMELINE_DISCONTINUITY ||
        (delivery->buffer && delivery->buffer->frame_count != f->context.frame_count))
        f->bad_delivery = true;
    if (delivery->buffer && delivery->buffer->frame_count)
        memcpy(f->delivered, delivery->buffer->data, delivery->buffer->frame_count * 2 * sizeof(float));
    return f->emit_result;
}

static OrpheusSlotId add_slot(void* user, const OrpheusSlotInfo* slot) {
    Fixture* f = (Fixture*)user;
    if (strcmp(slot->key, "current_gain") == 0) {
        f->probe_found = slot->kind == ORPHEUS_SLOT_PROBE &&
            slot->type == ORPHEUS_VALUE_FLOAT && (slot->flags & ORPHEUS_SLOT_READBACK) &&
            !(slot->flags & ORPHEUS_SLOT_PERSISTENT);
        f->probe_offset = slot->offset;
    }
    return ++f->slots;
}

static int init(Fixture* f, const OrpheusComponentInterface* api,
        const char** ids, const OrpheusValue* values, uint32_t count) {
    memset(f, 0, sizeof(*f));
    f->api = api;
    const OrpheusComponentDescriptor* descriptor = api->get_descriptor();
    CHECK(descriptor->abi_version == 7 && descriptor->port_count == 2);
    f->state = calloc(1, descriptor->state_size);
    CHECK(f->state);
    OrpheusConfig config = {.state_block=f->state, .channels=2, .sample_rate=1000,
        .block_size=4, .param_ids=ids, .param_values=values, .param_count=count};
    void* created = NULL;
    CHECK(api->create(&created, &config) == ORPHEUS_OK && created == f->state);
    CHECK(api->prepare(f->state, &config) == ORPHEUS_OK);
    for (unsigned i = 0; i < 8; ++i) f->input[i] = ((int)i - 4) * 0.25f;
    f->in = (OrpheusBuffer){.data=f->input, .format=ORPHEUS_FORMAT_F32,
        .channels=2, .frame_count=4, .frame_capacity=4, .interleaved=true};
    f->out = f->in;
    f->out.data = f->output;
    f->out.frame_count = 0;
    f->inputs[0] = &f->in;
    f->outputs[0] = &f->out;
    f->continuation = (OrpheusContinuation){.user=f, .emit=emit};
    f->context = (OrpheusProcessContext){.inputs=f->inputs, .outputs=f->outputs,
        .input_count=1, .output_count=1, .frame_count=4, .valid_frames=4,
        .sample_rate=1000, .frame_index=900, .epoch=3,
        .timeline_flags=ORPHEUS_TIMELINE_DISCONTINUITY, .continuation=&f->continuation};
    OrpheusRegistry registry = {.ctx=f, .add=add_slot};
    CHECK(api->register_slots(f->state, &registry) == ORPHEUS_OK);
    CHECK(f->slots == descriptor->param_count);
    return 0;
}

static int set(Fixture* f, const char* key, float value) {
    OrpheusValue parameter = FLOAT(value);
    CHECK(f->api->set_parameter(f->state, key, &parameter) == ORPHEUS_OK);
    OrpheusValue actual;
    CHECK(f->api->get_parameter(f->state, key, &actual) == ORPHEUS_OK);
    CHECK(actual.type == ORPHEUS_VALUE_FLOAT && actual.value.f32 == value);
    return 0;
}

static int process(Fixture* f) {
    unsigned before = f->emits;
    CHECK(f->api->process(f->state, &f->context) == ORPHEUS_OK);
    CHECK(f->emits == before + 1 && !f->bad_delivery);
    if (f->outputs[0] && f->context.frame_count)
        CHECK(!memcmp(f->delivered, f->outputs[0]->data, f->context.frame_count * 2 * sizeof(float)));
    return 0;
}

static int invalid(Fixture* f, const char* key, OrpheusValue value) {
    size_t size = f->api->get_descriptor()->state_size;
    void* before = malloc(size);
    CHECK(before);
    memcpy(before, f->state, size);
    CHECK(f->api->set_parameter(f->state, key, &value) == ORPHEUS_ERR_INVALID_ARG);
    CHECK(!memcmp(before, f->state, size));
    free(before);
    return 0;
}

static int finish(Fixture* f, bool optional) {
    f->context.frame_count = 2;
    for (unsigned i = 4; i < 8; ++i) f->output[i] = 1234.5f;
    CHECK(process(f) == 0);
    for (unsigned i = 4; i < 8; ++i) CHECK(f->output[i] == 1234.5f);
    f->context.frame_count = 4;
    unsigned before = f->emits;
    f->out.frame_capacity = 3;
    CHECK(f->api->process(f->state, &f->context) == ORPHEUS_ERR_INVALID_ARG);
    CHECK(f->emits == before);
    f->out.frame_capacity = 4;
    f->inputs[0] = NULL;
    if (optional) {
        CHECK(process(f) == 0);
        for (unsigned i = 0; i < 8; ++i) CHECK(f->output[i] == 0.0f);
        f->outputs[0] = NULL;
        CHECK(process(f) == 0);
    } else {
        CHECK(f->api->process(f->state, &f->context) == ORPHEUS_ERR_INVALID_ARG);
        CHECK(f->emits == before);
    }
    f->inputs[0] = &f->in;
    f->outputs[0] = &f->out;
    f->emit_result = -77;
    CHECK(f->api->process(f->state, &f->context) == -77);
    CHECK(!f->bad_delivery);
    CHECK(f->api->destroy(f->state) == ORPHEUS_OK);
    free(f->state);
    return 0;
}

static int test_balance(void) {
    Fixture f;
    const char* ids[] = {"balance", "ramp_ms"};
    OrpheusValue values[] = {FLOAT(0.25f), FLOAT(1.0f)};
    CHECK(init(&f, delivery_balance(), ids, values, 2) == 0);
    CHECK(invalid(&f, "balance", (OrpheusValue)FLOAT(1.1f)) == 0);
    CHECK(invalid(&f, "balance", (OrpheusValue){.type=ORPHEUS_VALUE_INT, .value.i32=1}) == 0);
    CHECK(process(&f) == 0);
    for (unsigned i = 0; i < 8; ++i)
        CHECK(f.output[i] == f.input[i] * (i % 2 ? 1.0f : 0.75f));
    CHECK(set(&f, "balance", -0.5f) == 0);
    CHECK(process(&f) == 0);
    for (unsigned n = 0; n < 4; ++n) {
        float decay = expf(-(float)(n + 1));
        CHECK(fabsf(f.output[2*n] - f.input[2*n] * (1.0f - 0.25f * decay)) < 2e-7f);
        CHECK(fabsf(f.output[2*n+1] - f.input[2*n+1] * (0.5f + 0.5f * decay)) < 2e-7f);
    }
    CHECK(f.api->reset(f.state) == ORPHEUS_OK && process(&f) == 0);
    CHECK(f.output[0] == -1.0f && f.output[1] == -0.375f);
    return finish(&f, false);
}

static int test_mute(void) {
    Fixture f;
    const char* ids[] = {"ramp_ms"};
    OrpheusValue values[] = {FLOAT(1.0f)};
    CHECK(init(&f, delivery_mute(), ids, values, 1) == 0);
    CHECK(invalid(&f, "mute", (OrpheusValue)FLOAT(2.0f)) == 0);
    CHECK(invalid(&f, "mute", (OrpheusValue){.type=ORPHEUS_VALUE_BOOL, .value.b=true}) == 0);
    CHECK(process(&f) == 0 && !memcmp(f.input, f.output, sizeof(f.input)));
    CHECK(set(&f, "mute", 1.0f) == 0 && process(&f) == 0);
    for (unsigned i = 0; i < 8; ++i)
        CHECK(fabsf(f.output[i] - f.input[i] * expf(-(float)(i+1))) < 2e-7f);
    CHECK(f.api->reset(f.state) == ORPHEUS_OK && process(&f) == 0);
    for (unsigned i = 0; i < 8; ++i) CHECK(f.output[i] == 0.0f);
    return finish(&f, false);
}

static int test_square(void) {
    Fixture f;
    CHECK(init(&f, delivery_square(), NULL, NULL, 0) == 0);
    CHECK(invalid(&f, "channels", (OrpheusValue)FLOAT(2.0f)) == 0);
    CHECK(process(&f) == 0);
    for (unsigned i = 0; i < 8; ++i) CHECK(f.output[i] == f.input[i] * f.input[i]);
    f.outputs[0] = &f.in;
    CHECK(process(&f) == 0);
    for (unsigned i = 0; i < 8; ++i) CHECK(f.input[i] == f.output[i]);
    f.outputs[0] = &f.out;
    return finish(&f, false);
}

static int test_saturation(void) {
    Fixture f;
    const char* ids[] = {"limit"};
    OrpheusValue values[] = {FLOAT(0.4f)};
    CHECK(init(&f, delivery_saturation(), ids, values, 1) == 0);
    CHECK(invalid(&f, "limit", (OrpheusValue)FLOAT(0.0f)) == 0);
    CHECK(process(&f) == 0);
    for (unsigned i = 0; i < 8; ++i)
        CHECK(f.output[i] == fmaxf(-0.4f, fminf(0.4f, f.input[i])));
    CHECK(set(&f, "soft", 0.35f) == 0 && set(&f, "limit", 0.6f) == 0);
    CHECK(process(&f) == 0);
    for (unsigned i = 0; i < 8; ++i) {
        float hard = fmaxf(-0.6f, fminf(0.6f, f.input[i]));
        float expected = 0.65f * hard + 0.35f * 0.6f * tanhf(f.input[i]/0.6f);
        CHECK(fabsf(f.output[i] - expected) < 2e-7f);
    }
    CHECK(f.api->reset(f.state) == ORPHEUS_OK && process(&f) == 0);
    CHECK(f.output[0] == -0.5f);
    return finish(&f, false);
}

static int test_soft_clipper(void) {
    Fixture f;
    const char* ids[] = {"drive_db"};
    OrpheusValue values[] = {FLOAT(6.0f)};
    CHECK(init(&f, delivery_soft_clipper(), ids, values, 1) == 0);
    CHECK(invalid(&f, "drive_db", (OrpheusValue)FLOAT(25.0f)) == 0);
    for (unsigned pass = 0; pass < 2; ++pass) {
        if (pass) CHECK(set(&f, "drive_db", -6.0f) == 0);
        CHECK(process(&f) == 0);
        float drive = powf(10.0f, (pass ? -6.0f : 6.0f)/20.0f);
        for (unsigned i = 0; i < 8; ++i)
            CHECK(fabsf(f.output[i] - tanhf(f.input[i]*drive)/tanhf(drive)) < 2e-7f);
    }
    CHECK(f.api->reset(f.state) == ORPHEUS_OK && process(&f) == 0);
    CHECK(fabsf(f.output[1] - tanhf(-0.75f)/tanhf(1.0f)) < 2e-7f);
    return finish(&f, false);
}

static int test_exponential_gain(void) {
    Fixture f;
    const char* ids[] = {"target_gain", "ramp_coefficient", "remaining_blocks", "initial_gain"};
    OrpheusValue values[] = {FLOAT(0.25f), FLOAT(0.5f), FLOAT(2.0f), FLOAT(2.0f)};
    CHECK(init(&f, delivery_exponential_gain(), ids, values, 4) == 0);
    CHECK(invalid(&f, "remaining_blocks", (OrpheusValue)FLOAT(0.5f)) == 0);
    CHECK(invalid(&f, "target_gain", (OrpheusValue)FLOAT(-1.0f)) == 0);
    CHECK(f.probe_found && process(&f) == 0);
    for (unsigned n = 0; n < 4; ++n)
        for (unsigned c = 0; c < 2; ++c)
            CHECK(f.output[2*n+c] == f.input[2*n+c] * ldexpf(1.0f, -(int)n));
    OrpheusValue probe;
    CHECK(f.api->get_parameter(f.state, "current_gain", &probe) == ORPHEUS_OK);
    CHECK(probe.value.f32 == 0.125f);
    CHECK(*(float*)((char*)f.state + f.probe_offset) == probe.value.f32);
    CHECK(f.api->get_parameter(f.state, "remaining_blocks", &probe) == ORPHEUS_OK && probe.value.f32 == 1.0f);
    CHECK(set(&f, "target_gain", 0.75f) == 0);
    CHECK(set(&f, "ramp_coefficient", 2.0f) == 0);
    CHECK(process(&f) == 0);
    for (unsigned n = 0; n < 4; ++n)
        for (unsigned c = 0; c < 2; ++c)
            CHECK(f.output[2*n+c] == f.input[2*n+c] * ldexpf(0.25f, (int)n));
    CHECK(f.api->reset(f.state) == ORPHEUS_OK);
    CHECK(f.api->get_parameter(f.state, "current_gain", &probe) == ORPHEUS_OK && probe.value.f32 == 2.0f);
    CHECK(f.api->get_parameter(f.state, "remaining_blocks", &probe) == ORPHEUS_OK && probe.value.f32 == 0.0f);
    f.context.frame_count = 0;
    CHECK(process(&f) == 0);
    CHECK(f.api->get_parameter(f.state, "current_gain", &probe) == ORPHEUS_OK && probe.value.f32 == 2.0f);
    f.context.frame_count = 4;
    return finish(&f, true);
}

int main(void) {
    CHECK(test_balance() == 0);
    CHECK(test_mute() == 0);
    CHECK(test_square() == 0);
    CHECK(test_saturation() == 0);
    CHECK(test_soft_clipper() == 0);
    CHECK(test_exponential_gain() == 0);
    puts("六组件数值、参数、探针与出口交付通过");
    return 0;
}
