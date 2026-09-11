#ifndef ORPHEUS_BRIDGE_STDIO_H
#define ORPHEUS_BRIDGE_STDIO_H

#include "orpheus_bridge_endpoint.h"
#include "orpheus_bridge_transport.h"

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

int orpheus_bridge_stdio_binary_mode(FILE* input, FILE* output);
int orpheus_bridge_stdio_serve(
    OrpheusBridgeEndpoint* endpoint, FILE* input, FILE* output);

#ifdef __cplusplus
}
#endif

#endif /* ORPHEUS_BRIDGE_STDIO_H */
