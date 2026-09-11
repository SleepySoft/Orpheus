#include "orpheus_bridge_stdio.h"

#include <string.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

typedef struct {
    FILE* input;
    FILE* output;
} StdioContext;

static int stdio_read(
    void* context, uint8_t* output, size_t length,
    size_t* count, int* eof) {
    StdioContext* stdio = (StdioContext*)context;
    FILE* input = stdio->input;
    *count = fread(output, 1u, length, input);
    if (*count != 0u) return 1;
    *eof = feof(input) != 0;
    return *eof ? 0 : -1;
}

static int stdio_write(
    void* context, const uint8_t* input, size_t length, size_t* count) {
    StdioContext* stdio = (StdioContext*)context;
    FILE* output = stdio->output;
    *count = fwrite(input, 1u, length, output);
    if (*count != length || fflush(output) != 0) return -1;
    return 1;
}

static void stdio_close(void* context) {
    (void)context;
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
    OrpheusBridgeChannel channel;
    if (endpoint == NULL || input == NULL || output == NULL) {
        return ORPHEUS_ERR_INVALID_ARG;
    }
    if (orpheus_bridge_stdio_binary_mode(input, output) != 0) {
        return ORPHEUS_ERR_PROCESSING;
    }

    StdioContext stdio;
    memset(&stdio, 0, sizeof(stdio));
    stdio.input = input;
    stdio.output = output;
    memset(&channel, 0, sizeof(channel));
    channel.context = &stdio;
    channel.read = stdio_read;
    channel.write = stdio_write;
    channel.close = stdio_close;
    return orpheus_bridge_channel_serve(endpoint, &channel);
}
