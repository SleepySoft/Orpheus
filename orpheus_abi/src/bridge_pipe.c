#include "orpheus_bridge_transport.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>

typedef struct {
    HANDLE handle;
} WindowsPipeContext;

static int windows_pipe_read(
    void* context, uint8_t* output, size_t length,
    size_t* count, int* eof) {
    WindowsPipeContext* pipe = (WindowsPipeContext*)context;
    DWORD read_bytes = 0u;
    if (!ReadFile(pipe->handle, output, (DWORD)length, &read_bytes, NULL)) {
        const DWORD error = GetLastError();
        if (error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_LISTENING) {
            *count = 0u;
            *eof = 1;
            return 0;
        }
        if (error == ERROR_MORE_DATA) {
            *count = (size_t)read_bytes;
            *eof = 0;
            return *count == 0u ? -1 : 1;
        }
        return -1;
    }
    *count = (size_t)read_bytes;
    *eof = read_bytes == 0u;
    return *eof ? 0 : 1;
}

static int windows_pipe_write(
    void* context, const uint8_t* input, size_t length, size_t* count) {
    WindowsPipeContext* pipe = (WindowsPipeContext*)context;
    DWORD written = 0u;
    if (!WriteFile(pipe->handle, input, (DWORD)length, &written, NULL) ||
        written != (DWORD)length) {
        return -1;
    }
    *count = (size_t)written;
    return 1;
}

static void windows_pipe_close(void* context) {
    WindowsPipeContext* pipe = (WindowsPipeContext*)context;
    if (pipe->handle != INVALID_HANDLE_VALUE) {
        FlushFileBuffers(pipe->handle);
        DisconnectNamedPipe(pipe->handle);
        CloseHandle(pipe->handle);
        pipe->handle = INVALID_HANDLE_VALUE;
    }
}

int orpheus_bridge_pipe_serve(
    OrpheusBridgeEndpoint* endpoint,
    const OrpheusBridgeServeConfig* config,
    char* endpoint_output,
    size_t endpoint_cap) {
    WindowsPipeContext pipe;
    OrpheusBridgeChannel channel;
    char name[256];
    const char* requested = config != NULL ? config->pipe_name : NULL;
    if (endpoint == NULL || config == NULL || requested == NULL ||
        requested[0] == '\0' || endpoint_output == NULL || endpoint_cap == 0u) {
        return ORPHEUS_ERR_INVALID_ARG;
    }
    if (strncmp(requested, "\\\\.\\pipe\\", 9u) == 0) {
        snprintf(name, sizeof(name), "%s", requested);
    } else {
        snprintf(name, sizeof(name), "\\\\.\\pipe\\%s", requested);
    }

    memset(&pipe, 0, sizeof(pipe));
    pipe.handle = CreateNamedPipeA(
        name, PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT |
            PIPE_REJECT_REMOTE_CLIENTS,
        1u, 8192u, 8192u, 0u, NULL);
    if (pipe.handle == INVALID_HANDLE_VALUE) return ORPHEUS_ERR_PROCESSING;

    snprintf(endpoint_output, endpoint_cap, "winpipe://%s", name + 9u);
    orpheus_bridge_announce(
        ORPHEUS_BRIDGE_TRANSPORT_PIPE, endpoint_output, config->endpoint_file);
    if (ConnectNamedPipe(pipe.handle, NULL) == 0 &&
        GetLastError() != ERROR_PIPE_CONNECTED) {
        windows_pipe_close(&pipe);
        return ORPHEUS_ERR_PROCESSING;
    }

    memset(&channel, 0, sizeof(channel));
    channel.context = &pipe;
    channel.read = windows_pipe_read;
    channel.write = windows_pipe_write;
    channel.close = windows_pipe_close;
    const int result = orpheus_bridge_channel_serve(endpoint, &channel);
    orpheus_bridge_channel_close(&channel);
    return result;
}

#elif !defined(ORPHEUS_BRIDGE_NO_LOCAL_SOCKETS)
#include <errno.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static int posix_pipe_read(
    void* context, uint8_t* output, size_t length,
    size_t* count, int* eof) {
    const int descriptor = (int)(intptr_t)context;
    const ssize_t got = recv(descriptor, output, length, 0);
    if (got == 0) {
        *count = 0u;
        *eof = 1;
        return 0;
    }
    if (got < 0) return -1;
    *count = (size_t)got;
    *eof = 0;
    return 1;
}

static int posix_pipe_write(
    void* context, const uint8_t* input, size_t length, size_t* count) {
    const int descriptor = (int)(intptr_t)context;
    const ssize_t written = send(descriptor, input, length, MSG_NOSIGNAL);
    if (written < 0) return -1;
    *count = (size_t)written;
    return written == (ssize_t)length ? 1 : -1;
}

static void posix_pipe_close(void* context) {
    const int descriptor = (int)(intptr_t)context;
    shutdown(descriptor, SHUT_RDWR);
    close(descriptor);
}

int orpheus_bridge_pipe_serve(
    OrpheusBridgeEndpoint* endpoint,
    const OrpheusBridgeServeConfig* config,
    char* endpoint_output,
    size_t endpoint_cap) {
    const char* requested = config != NULL ? config->pipe_name : NULL;
    if (endpoint == NULL || config == NULL || requested == NULL ||
        requested[0] == '\0' || endpoint_output == NULL || endpoint_cap == 0u) {
        return ORPHEUS_ERR_INVALID_ARG;
    }

    char address[108];
    if (requested[0] == '/') {
        snprintf(address, sizeof(address), "%s", requested);
    } else {
        snprintf(address, sizeof(address), "/tmp/orpheus-%s.sock", requested);
    }
    remove(address);

    int server = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server < 0) return ORPHEUS_ERR_PROCESSING;
    struct sockaddr_un socket_address;
    memset(&socket_address, 0, sizeof(socket_address));
    socket_address.sun_family = AF_UNIX;
    snprintf(socket_address.sun_path, sizeof(socket_address.sun_path),
        "%s", address);
    if (bind(server, (struct sockaddr*)&socket_address,
             sizeof(socket_address)) != 0 ||
        listen(server, 1) != 0) {
        close(server);
        remove(address);
        return ORPHEUS_ERR_PROCESSING;
    }

    snprintf(endpoint_output, endpoint_cap, "unix://%s", address);
    orpheus_bridge_announce(
        ORPHEUS_BRIDGE_TRANSPORT_PIPE, endpoint_output, config->endpoint_file);
    int client = accept(server, NULL, NULL);
    close(server);
    remove(address);
    if (client < 0) return ORPHEUS_ERR_PROCESSING;

    OrpheusBridgeChannel channel;
    memset(&channel, 0, sizeof(channel));
    channel.context = (void*)(intptr_t)client;
    channel.read = posix_pipe_read;
    channel.write = posix_pipe_write;
    channel.close = posix_pipe_close;
    const int result = orpheus_bridge_channel_serve(endpoint, &channel);
    orpheus_bridge_channel_close(&channel);
    return result;
}

#else

int orpheus_bridge_pipe_serve(
    OrpheusBridgeEndpoint* endpoint,
    const OrpheusBridgeServeConfig* config,
    char* endpoint_output,
    size_t endpoint_cap) {
    (void)endpoint;
    (void)config;
    (void)endpoint_output;
    (void)endpoint_cap;
    return ORPHEUS_ERR_UNSUPPORTED;
}

#endif
