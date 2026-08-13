#ifndef ORPHEUS_NOISE_DETECTOR_NLMS_H
#define ORPHEUS_NOISE_DETECTOR_NLMS_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NDNLMS_MAX_CH 32

typedef struct {
    uint32_t channels;
    uint32_t filter_length;   /* NLMS tap count per channel */
    float mu;                 /* normalized step size */
    float leak;               /* leakage factor (<=1) for stability */
    float eps;                /* regularization (avoid div-by-zero) */
    float time_thres;         /* residual amplitude threshold for "noisy sample" */
    float frame_thres_db;     /* residual-energy budget (dB) that flags a noisy frame */

    float* w;                 /* taps, per channel x filter_length */
    float* x;                 /* circular delay line of ref, per channel x filter_length */
    uint32_t* pos;            /* current write index per channel */

    /* probes */
    uint64_t total_frames;
    uint32_t noise_frames;
    float noise_ratio;        /* noise_frames/total_frames */
    uint64_t total_samples;
    uint64_t noisy_samples;
    uint32_t clicks;
    float residue_db;         /* smoothed residual energy in dB (residue vs in energy) */
    float echo_return_loss_db;/* ERL ~ 10log10(P_in / P_ref) proxy */
    float residue_pk;
    uint64_t erle_frames;
    double acc_erin;          /* accumulated |in|^2 for ERLE */
    double acc_eres;          /* accumulated |residue|^2 for ERLE */
    char json_detail[1024];
} NoiseDetectorNlmsState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif
