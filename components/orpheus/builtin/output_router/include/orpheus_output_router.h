#ifndef ORPHEUS_OUTPUT_ROUTER_H
#define ORPHEUS_OUTPUT_ROUTER_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_CH 32

typedef struct {
    float matrix[MAX_CH * MAX_CH];  /* row-major: [out*MAX_CH + in] */
    uint32_t channels_in;
    uint32_t channels_out;
    char matrix_str[2048];          /* 参数缓存（restart_required） */
} OutputRouterState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif /* ORPHEUS_OUTPUT_ROUTER_H */
