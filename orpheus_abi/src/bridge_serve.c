#include "orpheus_bridge_transport.h"
#include "orpheus_bridge_stdio.h"

#include <stdio.h>
#include <string.h>

int orpheus_bridge_serve(
    OrpheusBridgeEndpoint* endpoint,
    const OrpheusBridgeServeConfig* config,
    char* endpoint_output,
    size_t endpoint_cap) {
    if (endpoint == NULL || config == NULL || endpoint_output == NULL ||
        endpoint_cap == 0u) {
        return ORPHEUS_ERR_INVALID_ARG;
    }
    snprintf(endpoint_output, endpoint_cap, "stdio://");
    switch (config->transport) {
        case ORPHEUS_BRIDGE_TRANSPORT_STDIO: {
            orpheus_bridge_announce(
                ORPHEUS_BRIDGE_TRANSPORT_STDIO, endpoint_output,
                config->endpoint_file);
            return orpheus_bridge_stdio_serve(endpoint, stdin, stdout);
        }
        case ORPHEUS_BRIDGE_TRANSPORT_PIPE:
            return orpheus_bridge_pipe_serve(
                endpoint, config, endpoint_output, endpoint_cap);
        case ORPHEUS_BRIDGE_TRANSPORT_TCP:
            return orpheus_bridge_tcp_serve(
                endpoint, config, endpoint_output, endpoint_cap);
        default:
            return ORPHEUS_ERR_UNSUPPORTED;
    }
}
