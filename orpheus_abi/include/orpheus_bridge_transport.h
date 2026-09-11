#ifndef ORPHEUS_BRIDGE_TRANSPORT_H
#define ORPHEUS_BRIDGE_TRANSPORT_H

#include "orpheus_bridge_endpoint.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ORPHEUS_BRIDGE_TRANSPORT_STDIO = 1,
    ORPHEUS_BRIDGE_TRANSPORT_PIPE = 2,
    ORPHEUS_BRIDGE_TRANSPORT_TCP = 3
} OrpheusBridgeTransport;

typedef struct {
    void* context;
    int (*read)(void* context, uint8_t* output, size_t length,
                size_t* count, int* eof);
    int (*write)(void* context, const uint8_t* input, size_t length,
                 size_t* count);
    void (*close)(void* context);
} OrpheusBridgeChannel;

typedef struct {
    OrpheusBridgeTransport transport;
    const char* pipe_name;
    const char* host;
    uint16_t port;
    const char* endpoint_file;
} OrpheusBridgeServeConfig;

int orpheus_bridge_channel_serve(
    OrpheusBridgeEndpoint* endpoint,
    const OrpheusBridgeChannel* channel);

void orpheus_bridge_channel_close(const OrpheusBridgeChannel* channel);

void orpheus_bridge_announce(
    OrpheusBridgeTransport transport,
    const char* endpoint,
    const char* endpoint_file);

int orpheus_bridge_serve(
    OrpheusBridgeEndpoint* endpoint,
    const OrpheusBridgeServeConfig* config,
    char* endpoint_output,
    size_t endpoint_cap);

int orpheus_bridge_pipe_serve(
    OrpheusBridgeEndpoint* endpoint,
    const OrpheusBridgeServeConfig* config,
    char* endpoint_output,
    size_t endpoint_cap);

int orpheus_bridge_tcp_serve(
    OrpheusBridgeEndpoint* endpoint,
    const OrpheusBridgeServeConfig* config,
    char* endpoint_output,
    size_t endpoint_cap);

#ifdef __cplusplus
}
#endif

#endif
