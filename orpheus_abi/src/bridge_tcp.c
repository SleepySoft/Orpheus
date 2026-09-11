#include "orpheus_bridge_transport.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

typedef struct {
    int descriptor;
#ifdef _WIN32
    SOCKET socket;
#endif
} TcpContext;

static int tcp_read(
    void* context, uint8_t* output, size_t length,
    size_t* count, int* eof) {
    TcpContext* tcp = (TcpContext*)context;
#ifdef _WIN32
    const int received = recv(tcp->socket, (char*)output, (int)length, 0);
#else
    const ssize_t received = recv(tcp->descriptor, output, length, 0);
#endif
    if (received == 0) {
        *count = 0u;
        *eof = 1;
        return 0;
    }
    if (received < 0) return -1;
    *count = (size_t)received;
    *eof = 0;
    return 1;
}

static int tcp_write(
    void* context, const uint8_t* input, size_t length, size_t* count) {
    TcpContext* tcp = (TcpContext*)context;
#ifdef _WIN32
    const int written = send(tcp->socket, (const char*)input, (int)length, 0);
#else
    const ssize_t written = send(tcp->descriptor, input, length, MSG_NOSIGNAL);
#endif
    if (written < 0) return -1;
    *count = (size_t)written;
    return *count == length ? 1 : -1;
}

static void tcp_close(void* context) {
    TcpContext* tcp = (TcpContext*)context;
#ifdef _WIN32
    if (tcp->socket != INVALID_SOCKET) {
        shutdown(tcp->socket, SD_BOTH);
        closesocket(tcp->socket);
        tcp->socket = INVALID_SOCKET;
    }
#else
    if (tcp->descriptor >= 0) {
        shutdown(tcp->descriptor, SHUT_RDWR);
        close(tcp->descriptor);
        tcp->descriptor = -1;
    }
#endif
}

int orpheus_bridge_tcp_serve(
    OrpheusBridgeEndpoint* endpoint,
    const OrpheusBridgeServeConfig* config,
    char* endpoint_output,
    size_t endpoint_cap) {
    const char* host = config != NULL && config->host != NULL &&
        config->host[0] != '\0' ? config->host : "127.0.0.1";
    if (endpoint == NULL || config == NULL || endpoint_output == NULL ||
        endpoint_cap == 0u) {
        return ORPHEUS_ERR_INVALID_ARG;
    }

#ifdef _WIN32
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        return ORPHEUS_ERR_PROCESSING;
    }
#endif

    char port_text[8];
    snprintf(port_text, sizeof(port_text), "%u", (unsigned)config->port);
    struct addrinfo hints;
    struct addrinfo* addresses = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_NUMERICHOST | AI_PASSIVE;
    if (getaddrinfo(host, port_text, &hints, &addresses) != 0 ||
        addresses == NULL) {
#ifdef _WIN32
        WSACleanup();
#endif
        return ORPHEUS_ERR_PROCESSING;
    }

    int server = socket(
        addresses->ai_family, addresses->ai_socktype, addresses->ai_protocol);
    if (server < 0) {
        freeaddrinfo(addresses);
#ifdef _WIN32
        WSACleanup();
#endif
        return ORPHEUS_ERR_PROCESSING;
    }
    int reuse = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR,
        (const char*)&reuse, sizeof(reuse));
    if (bind(server, addresses->ai_addr, addresses->ai_addrlen) != 0 ||
        listen(server, 1) != 0) {
        freeaddrinfo(addresses);
#ifdef _WIN32
        closesocket(server);
        WSACleanup();
#else
        close(server);
#endif
        return ORPHEUS_ERR_PROCESSING;
    }

    struct sockaddr_storage bound_address;
    socklen_t bound_length = sizeof(bound_address);
    if (getsockname(server, (struct sockaddr*)&bound_address,
                    &bound_length) != 0) {
        freeaddrinfo(addresses);
#ifdef _WIN32
        closesocket(server);
        WSACleanup();
#else
        close(server);
#endif
        return ORPHEUS_ERR_PROCESSING;
    }
    const uint16_t bound_port = ntohs(
        bound_address.ss_family == AF_INET6
            ? ((struct sockaddr_in6*)&bound_address)->sin6_port
            : ((struct sockaddr_in*)&bound_address)->sin_port);
    snprintf(endpoint_output, endpoint_cap, "tcp://%s:%u", host,
        (unsigned)bound_port);
    freeaddrinfo(addresses);
    orpheus_bridge_announce(
        ORPHEUS_BRIDGE_TRANSPORT_TCP, endpoint_output, config->endpoint_file);

#ifdef _WIN32
    SOCKET client = accept(server, NULL, NULL);
    closesocket(server);
    if (client == INVALID_SOCKET) {
        WSACleanup();
        return ORPHEUS_ERR_PROCESSING;
    }
#else
    int client = accept(server, NULL, NULL);
    close(server);
    if (client < 0) return ORPHEUS_ERR_PROCESSING;
#endif

    TcpContext context;
#ifdef _WIN32
    context.socket = client;
#else
    context.descriptor = client;
#endif
    OrpheusBridgeChannel channel;
    memset(&channel, 0, sizeof(channel));
    channel.context = &context;
    channel.read = tcp_read;
    channel.write = tcp_write;
    channel.close = tcp_close;
    const int result = orpheus_bridge_channel_serve(endpoint, &channel);
    orpheus_bridge_channel_close(&channel);
#ifdef _WIN32
    WSACleanup();
#endif
    return result;
}
