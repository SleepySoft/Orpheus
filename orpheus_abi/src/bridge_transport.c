#include "orpheus_bridge_transport.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

static int channel_read_exact(
    const OrpheusBridgeChannel* channel,
    uint8_t* output,
    size_t length,
    int* eof) {
    size_t offset = 0u;
    if (channel == NULL || channel->read == NULL || output == NULL ||
        eof == NULL) {
        return -1;
    }
    *eof = 0;
    while (offset < length) {
        size_t count = 0u;
        int result = channel->read(
            channel->context, output + offset, length - offset, &count, eof);
        if (result == 0) return offset == 0u ? 0 : -1;
        if (result < 0 || count == 0u) return -1;
        offset += count;
    }
    return 1;
}

static int channel_write_exact(
    const OrpheusBridgeChannel* channel,
    const uint8_t* input,
    size_t length) {
    size_t offset = 0u;
    if (channel == NULL || channel->write == NULL) return -1;
    while (offset < length) {
        size_t count = 0u;
        int result = channel->write(
            channel->context, input + offset, length - offset, &count);
        if (result <= 0 || count == 0u) return -1;
        offset += count;
    }
    return 1;
}

int orpheus_bridge_channel_serve(
    OrpheusBridgeEndpoint* endpoint,
    const OrpheusBridgeChannel* channel) {
    uint8_t request[ORPHEUS_BRIDGE_MAX_MESSAGE];
    uint8_t response[ORPHEUS_BRIDGE_MAX_MESSAGE];
    if (endpoint == NULL || channel == NULL || channel->close == NULL) {
        return ORPHEUS_ERR_INVALID_ARG;
    }

    for (;;) {
        uint32_t request_len = 0u;
        int eof = 0;
        int read_result = channel_read_exact(
            channel, (uint8_t*)&request_len, sizeof(request_len), &eof);
        if (read_result == 0) return ORPHEUS_OK;
        if (read_result < 0 || request_len < sizeof(OrpheusMessageHeader) ||
            request_len > sizeof(request)) {
            return ORPHEUS_ERR_INVALID_ARG;
        }
        read_result = channel_read_exact(channel, request, request_len, &eof);
        if (read_result <= 0) return ORPHEUS_ERR_PROCESSING;

        size_t response_len = 0u;
        int result = orpheus_bridge_endpoint_process(
            endpoint, request, request_len,
            response, sizeof(response), &response_len);
        if (result != ORPHEUS_OK) return result;
        if (response_len > UINT32_MAX) return ORPHEUS_ERR_OUT_OF_MEMORY;

        uint32_t wire_len = (uint32_t)response_len;
        if (channel_write_exact(
                channel, (const uint8_t*)&wire_len, sizeof(wire_len)) < 0 ||
            channel_write_exact(channel, response, response_len) < 0) {
            return ORPHEUS_ERR_PROCESSING;
        }
        if (orpheus_bridge_endpoint_should_stop(endpoint)) return ORPHEUS_OK;
    }
}

void orpheus_bridge_channel_close(const OrpheusBridgeChannel* channel) {
    if (channel != NULL && channel->close != NULL) {
        channel->close(channel->context);
    }
}

static void json_escape(const char* input, char* output, size_t cap) {
    size_t used = 0u;
    for (size_t i = 0u; input && input[i] != 0 && used + 2u < cap; ++i) {
        const char ch = input[i];
        if (ch == '"' || ch == '\\') output[used++] = '\\';
        output[used++] = ch == '\n' ? ' ' : ch;
    }
    output[used] = 0;
}

void orpheus_bridge_announce(
    OrpheusBridgeTransport transport,
    const char* endpoint,
    const char* endpoint_file) {
    char escaped[512];
    const int pid = 
#ifdef _WIN32
        _getpid();
#else
        (int)getpid();
#endif
    (void)transport;
    if (endpoint == NULL) return;
    json_escape(endpoint, escaped, sizeof(escaped));
    fprintf(stderr, "BRIDGE_READY {\"protocol\":%u,\"pid\":%d,"
        "\"endpoints\":[\"%s\"]}\n",
        ORPHEUS_BRIDGE_PROTOCOL_VERSION, pid, escaped);
    if (endpoint_file != NULL && endpoint_file[0] != '\0') {
        FILE* output = fopen(endpoint_file, "w");
        if (output != NULL) {
            fprintf(output, "{\"state\":\"ready\",\"pid\":%d,"
                "\"endpoints\":[\"%s\"]}\n", pid, escaped);
            fclose(output);
        }
    }
}
