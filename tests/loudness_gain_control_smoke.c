#include "orpheus_loudness_gain_control.h"

#include <math.h>
#include <string.h>

#define FRAMES 128
#define CHANNELS 2

static int set_level(const OrpheusComponentInterface* iface, void* state, float level) {
    OrpheusValue value = {.type=ORPHEUS_VALUE_FLOAT, .value.f32=level};
    return iface->set_parameter(state, "level", &value);
}

static int read_float(const OrpheusComponentInterface* iface, void* state,
                      const char* id, float* result) {
    OrpheusValue value;
    if (iface->get_parameter(state, id, &value) != ORPHEUS_OK) return -1;
    if (value.type != ORPHEUS_VALUE_FLOAT) return -2;
    *result = value.value.f32;
    return 0;
}

static int process_blocks(const OrpheusComponentInterface* iface, void* state,
                          float level, int blocks) {
    float input[FRAMES * CHANNELS];
    float output[FRAMES * CHANNELS];
    for (size_t i = 0; i < FRAMES * CHANNELS; ++i) input[i] = (float)i / 1000.0f;
    OrpheusBuffer input_buffer = {
        input, ORPHEUS_FORMAT_F32, CHANNELS, FRAMES, FRAMES, true
    };
    OrpheusBuffer output_buffer = {
        output, ORPHEUS_FORMAT_F32, CHANNELS, FRAMES, FRAMES, true
    };
    const OrpheusBuffer* inputs[] = {&input_buffer};
    OrpheusBuffer* outputs[] = {&output_buffer};
    OrpheusProcessContext context = {
        .state=state, .inputs=inputs, .outputs=outputs,
        .input_count=1, .output_count=1, .frame_count=FRAMES,
        .sample_rate=48000, .valid_frames=FRAMES,
    };

    if (set_level(iface, state, level) != ORPHEUS_OK) return -1;
    for (int block = 0; block < blocks; ++block) {
        memset(output, 0, sizeof(output));
        if (iface->process(state, &context) != ORPHEUS_OK) return -2;
        if (memcmp(input, output, sizeof(input)) != 0) return -3;
    }
    return 0;
}

int main(void) {
    const OrpheusComponentInterface* iface = orpheus_get_interface();
    if (iface == NULL) return 1;

    const char* ids[] = {
        "level", "target_db", "min_gain_db", "max_gain_db", "gate_db",
        "detector_attack_ms", "detector_release_ms",
        "gain_attack_ms", "gain_release_ms", "channels"
    };
    OrpheusValue values[10];
    memset(values, 0, sizeof(values));
    for (int i = 0; i < 9; ++i) values[i].type = ORPHEUS_VALUE_FLOAT;
    values[0].value.f32 = 0.0f;
    values[1].value.f32 = -18.0f;
    values[2].value.f32 = -12.0f;
    values[3].value.f32 = 12.0f;
    values[4].value.f32 = -55.0f;
    values[5].value.f32 = 5.0f;
    values[6].value.f32 = 10.0f;
    values[7].value.f32 = 5.0f;
    values[8].value.f32 = 10.0f;
    values[9].type = ORPHEUS_VALUE_INT;
    values[9].value.i32 = CHANNELS;

    LoudnessGainControlState memory;
    memset(&memory, 0, sizeof(memory));
    OrpheusConfig config = {
        .sample_rate=48000, .block_size=FRAMES, .channels=CHANNELS,
        .param_ids=ids, .param_values=values, .param_count=10,
        .state_block=&memory,
    };
    void* state = NULL;
    if (iface->create(&state, &config) != ORPHEUS_OK) return 2;
    if (iface->prepare(state, &config) != ORPHEUS_OK) return 3;

    float gain_db = 0.0f;
    float output_db = 0.0f;
    if (process_blocks(iface, state, 0.05f, 300) != 0) return 4;
    if (read_float(iface, state, "gain_db", &gain_db) != 0) return 5;
    if (read_float(iface, state, "output_db", &output_db) != 0) return 6;
    if (gain_db < 7.8f || gain_db > 8.3f) return 7;
    if (output_db < -18.2f || output_db > -17.8f) return 8;

    if (process_blocks(iface, state, 0.5f, 300) != 0) return 9;
    if (read_float(iface, state, "gain_db", &gain_db) != 0) return 10;
    if (read_float(iface, state, "output_db", &output_db) != 0) return 11;
    if (gain_db < -12.1f || gain_db > -11.7f) return 12;
    if (output_db < -18.2f || output_db > -17.7f) return 13;

    if (iface->reset(state) != ORPHEUS_OK) return 14;
    if (process_blocks(iface, state, 0.0f, 100) != 0) return 15;
    if (read_float(iface, state, "gain_db", &gain_db) != 0) return 16;
    if (fabsf(gain_db) > 0.001f) return 17;
    return 0;
}
