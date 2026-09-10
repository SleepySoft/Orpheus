#ifndef ORPHEUS_LOUDNESS_GAIN_CONTROL_H
#define ORPHEUS_LOUDNESS_GAIN_CONTROL_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float level;
    float target_db;
    float min_gain_db;
    float max_gain_db;
    float gate_db;
    float detector_attack_ms;
    float detector_release_ms;
    float gain_attack_ms;
    float gain_release_ms;
    uint32_t channels;

    float detector_energy;
    float gain_db;
    float input_db;
    float output_db;
} LoudnessGainControlState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif /* ORPHEUS_LOUDNESS_GAIN_CONTROL_H */
