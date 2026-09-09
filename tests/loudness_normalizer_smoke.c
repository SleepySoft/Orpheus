#include "orpheus_loudness_normalizer.h"

#include <math.h>
#include <string.h>

#define FRAMES 128
#define CHANNELS 2

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
    values[5].value.f32 = 10.0f;
    values[6].value.f32 = 5.0f;
    values[7].value.f32 = 10.0f;
    values[8].type = ORPHEUS_VALUE_INT;
    values[8].value.i32 = CHANNELS;

    LoudnessNormalizerState state_memory;
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
    return 0;
}
