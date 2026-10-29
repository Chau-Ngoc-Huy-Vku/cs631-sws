#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#define PORT 8080
#define BACKLOG 10

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

int
main(void)
{
    int server_fd;
    int reuse = 1;
    struct sockaddr_in server_addr;

    signal(SIGPIPE, SIG_IGN);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR,
                   &reuse, sizeof(reuse)) < 0) {
        perror("setsockopt");
        close(server_fd);
        return 1;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&server_addr,
             sizeof(server_addr)) < 0) {
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
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(
            server_fd,
            (struct sockaddr *)&client_addr,
            &client_len
        );

        if (client_fd < 0) {
            if (errno == EINTR)
                continue;

            perror("accept");
            continue;
        }

        char request[4096];
        ssize_t n;

        do {
            n = recv(client_fd, request, sizeof(request), 0);
        } while (n < 0 && errno == EINTR);

        if (n < 0) {
            perror("recv");
        } else if (n > 0) {
            static const char body[] =
                "<!doctype html>\n"
                "<html><body>"
                "<h1>CS631 Simple Web Server</h1>"
                "<p>Hello from our server!</p>"
                "</body></html>\n";

            char headers[256];

            int header_len = snprintf(
                headers,
                sizeof(headers),
                "HTTP/1.0 200 OK\r\n"
                "Content-Type: text/html; charset=utf-8\r\n"
                "Content-Length: %zu\r\n"
                "Connection: close\r\n"
                "\r\n",
                sizeof(body) - 1
            );

            if (header_len < 0 ||
                (size_t)header_len >= sizeof(headers)) {
                fprintf(stderr, "Could not build HTTP response\n");
            } else if (
                send_all(client_fd, headers, (size_t)header_len) < 0 ||
                send_all(client_fd, body, sizeof(body) - 1) < 0
            ) {
                perror("send");
            }
        }

        close(client_fd);
    }

    close(server_fd);
    return 0;
}
