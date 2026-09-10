#include "orpheus_bridge_stdio.h"

#include <string.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

static int read_exact(FILE* input, uint8_t* output, size_t length) {
    size_t offset = 0u;
    while (offset < length) {
        size_t count = fread(output + offset, 1u, length - offset, input);
        if (count == 0u) return feof(input) ? 0 : -1;
        offset += count;
    }
    return 1;
}

static int write_exact(FILE* output, const uint8_t* data, size_t length) {
    size_t offset = 0u;
    while (offset < length) {
        size_t count = fwrite(data + offset, 1u, length - offset, output);
        if (count == 0u) return -1;
        offset += count;
    }
    return fflush(output) == 0 ? 0 : -1;
}

int orpheus_bridge_stdio_binary_mode(FILE* input, FILE* output) {
#ifdef _WIN32
    if (_setmode(_fileno(input), _O_BINARY) == -1) return -1;
    if (_setmode(_fileno(output), _O_BINARY) == -1) return -1;
#else
    (void)input;
    (void)output;
#endif
    return 0;
}

int orpheus_bridge_stdio_serve(
    OrpheusBridgeEndpoint* endpoint, FILE* input, FILE* output) {
    if (endpoint == NULL || input == NULL || output == NULL) return ORPHEUS_ERR_INVALID_ARG;
    if (orpheus_bridge_stdio_binary_mode(input, output) != 0) return ORPHEUS_ERR_PROCESSING;

    uint8_t request[ORPHEUS_BRIDGE_MAX_MESSAGE];
    uint8_t response[ORPHEUS_BRIDGE_MAX_MESSAGE];
    for (;;) {
        uint32_t request_len = 0u;
        int read_result = read_exact(input, (uint8_t*)&request_len, sizeof(request_len));
        if (read_result == 0) return ORPHEUS_OK;
        if (read_result < 0 || request_len < sizeof(OrpheusMessageHeader) ||
            request_len > sizeof(request)) return ORPHEUS_ERR_INVALID_ARG;
        read_result = read_exact(input, request, request_len);
        if (read_result <= 0) return ORPHEUS_ERR_PROCESSING;

        size_t response_len = 0u;
        int result = orpheus_bridge_endpoint_process(
            endpoint, request, request_len,
            response, sizeof(response), &response_len);
        if (result != ORPHEUS_OK) return result;
        if (response_len > UINT32_MAX) return ORPHEUS_ERR_OUT_OF_MEMORY;
        uint32_t wire_len = (uint32_t)response_len;
        if (write_exact(output, (const uint8_t*)&wire_len, sizeof(wire_len)) != 0 ||
            write_exact(output, response, response_len) != 0) {
            return ORPHEUS_ERR_PROCESSING;
        }
        if (orpheus_bridge_endpoint_should_stop(endpoint)) return ORPHEUS_OK;
    }
}
