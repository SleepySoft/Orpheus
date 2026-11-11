#ifndef ORPHEUS_BUILTIN_DEVICE_IN_ALGORITHM_H
#define ORPHEUS_BUILTIN_DEVICE_IN_ALGORITHM_H

typedef struct {
    double sample_rate;
    uint64_t frame_index;
    uint32_t block_size;
} orpheus_builtin_device_in_AlgorithmState;

#endif
