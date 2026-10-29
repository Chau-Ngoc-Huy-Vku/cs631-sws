#define _POSIX_C_SOURCE 200809L

#include "http.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#define PORT 8080
#define BACKLOG 10
#define REQUEST_SIZE 8192

static int
send_all(int fd, const char *buffer, size_t length)
{
    size_t sent = 0;

    while (sent < length) {
        ssize_t n = send(fd, buffer + sent, length - sent, 0);

        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }

        if (n == 0)
            return -1;

        sent += (size_t)n;
    }

    return 0;
}

static int
send_response(int fd, int status, const char *reason,
              const char *body)
{
    char header[512];
    size_t body_len = strlen(body);

    int header_len = snprintf(
        header, sizeof(header),
        "HTTP/1.0 %d %s\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n",
        status, reason, body_len
    );

    if (header_len < 0 || (size_t)header_len >= sizeof(header))
        return -1;

    if (send_all(fd, header, (size_t)header_len) < 0)
        return -1;

    return send_all(fd, body, body_len);
}

/* Read until the end of the HTTP headers is received. */
static ssize_t
read_request(int fd, char *buffer, size_t capacity)
{
    size_t used = 0;

    while (used < capacity - 1) {
        ssize_t n = recv(fd, buffer + used,
                         capacity - 1 - used, 0);

        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }

        if (n == 0)
            return -1;

        used += (size_t)n;
        buffer[used] = '\0';

        for (size_t i = 3; i < used; i++) {
            if (buffer[i - 3] == '\r' &&
                buffer[i - 2] == '\n' &&
                buffer[i - 1] == '\r' &&
                buffer[i] == '\n')
                return (ssize_t)used;
        }
    }

    return -1;
}

int
main(void)
{
    signal(SIGPIPE, SIG_IGN);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }

    int reuse = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR,
                   &reuse, sizeof(reuse)) < 0) {
        perror("setsockopt");
        close(server_fd);
        return 1;
    }

    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address,
             sizeof(address)) < 0) {
        perror("bind");
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, BACKLOG) < 0) {
        perror("listen");
        close(server_fd);
        return 1;
    }

    fprintf(stderr, "Test server listening on port %d\n", PORT);

    for (;;) {
        int client_fd = accept(server_fd, NULL, NULL);

        if (client_fd < 0) {
            if (errno == EINTR)
                continue;
            perror("accept");
            continue;
        }

        char buffer[REQUEST_SIZE];
        ssize_t length = read_request(
            client_fd, buffer, sizeof(buffer)
        );

        if (length < 0) {
            send_response(client_fd, 400, "Bad Request",
                          "<html><body><h1>400 Bad Request</h1>"
                          "</body></html>\n");
        } else {
            struct http_request request;

            if (http_parse_request(buffer, (size_t)length,
                                   &request) < 0) {
                send_response(client_fd, 400, "Bad Request",
                              "<html><body><h1>400 Bad Request</h1>"
                              "</body></html>\n");
            } else if (strcmp(request.method, "GET") != 0) {
                send_response(client_fd, 501, "Not Implemented",
                              "<html><body><h1>501 Not Implemented"
                              "</h1></body></html>\n");
            } else if (strcmp(request.uri, "/") != 0) {
                send_response(client_fd, 404, "Not Found",
                              "<html><body><h1>404 Not Found</h1>"
                              "</body></html>\n");
            } else {
                send_response(client_fd, 200, "OK",
                              "<!doctype html>\n"
                              "<html><body>"
                              "<h1>CS631 Simple Web Server</h1>"
                              "<p>HTTP request parsed successfully.</p>"
                              "</body></html>\n");
            }
        }

        close(client_fd);
    }

    close(server_fd);
    return 0;
}
