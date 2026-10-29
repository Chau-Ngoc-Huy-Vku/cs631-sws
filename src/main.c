#define _XOPEN_SOURCE 700

#include "http.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <unistd.h>

#define PORT 8080
#define BACKLOG 10
#define REQUEST_SIZE 8192

static int
send_all(int fd, const void *data, size_t length)
{
    const char *buffer = data;
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
              const char *content_type,
              const void *body, size_t body_length)
{
    char header[512];

    int header_length = snprintf(
        header, sizeof(header),
        "HTTP/1.0 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n",
        status, reason, content_type, body_length
    );

    if (header_length < 0 ||
        (size_t)header_length >= sizeof(header))
        return -1;

    if (send_all(fd, header, (size_t)header_length) < 0)
        return -1;

    if (body_length > 0)
        return send_all(fd, body, body_length);

    return 0;
}

static int
send_error(int fd, int status, const char *reason)
{
    char body[256];

    int length = snprintf(
        body, sizeof(body),
        "<!doctype html><html><body>"
        "<h1>%d %s</h1>"
        "</body></html>\n",
        status, reason
    );

    if (length < 0 || (size_t)length >= sizeof(body))
        return -1;

    return send_response(
        fd, status, reason, "text/html; charset=utf-8",
        body, (size_t)length
    );
}

static ssize_t
read_request(int fd, char *buffer, size_t capacity)
{
    size_t used = 0;

    while (used < capacity - 1) {
        ssize_t n = recv(
            fd, buffer + used, capacity - 1 - used, 0
        );

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
                buffer[i] == '\n') {
                return (ssize_t)used;
            }
        }
    }

    return -1;
}

/* Reject "." and ".." path components. */
static int
has_dot_segment(const char *path)
{
    const char *p = path;

    while (*p != '\0') {
        while (*p == '/')
            p++;

        if (*p == '\0')
            break;

        const char *start = p;

        while (*p != '\0' && *p != '/')
            p++;

        size_t length = (size_t)(p - start);

        if ((length == 1 && start[0] == '.') ||
            (length == 2 && start[0] == '.' &&
             start[1] == '.')) {
            return 1;
        }
    }

    return 0;
}

static const char *
get_content_type(const char *path)
{
    const char *ext = strrchr(path, '.');

    if (ext == NULL)
        return "application/octet-stream";

    if (strcmp(ext, ".html") == 0 ||
        strcmp(ext, ".htm") == 0)
        return "text/html; charset=utf-8";

    if (strcmp(ext, ".txt") == 0)
        return "text/plain; charset=utf-8";

    if (strcmp(ext, ".css") == 0)
        return "text/css";

    if (strcmp(ext, ".js") == 0)
        return "application/javascript";

    if (strcmp(ext, ".png") == 0)
        return "image/png";

    if (strcmp(ext, ".jpg") == 0 ||
        strcmp(ext, ".jpeg") == 0)
        return "image/jpeg";

    if (strcmp(ext, ".gif") == 0)
        return "image/gif";

    if (strcmp(ext, ".svg") == 0)
        return "image/svg+xml";

    return "application/octet-stream";
}

static int
serve_static_file(int client_fd, const char *uri)
{
    char path[HTTP_URI_MAX];
    char candidate[PATH_MAX];
    char root_real[PATH_MAX];
    char file_real[PATH_MAX];

    /* Query parameters are not part of the file name. */
    size_t path_length = strcspn(uri, "?");

    if (path_length == 0 ||
        path_length >= sizeof(path)) {
        return send_error(client_fd, 400, "Bad Request");
    }

    memcpy(path, uri, path_length);
    path[path_length] = '\0';

    if (path[0] != '/') {
        return send_error(client_fd, 400, "Bad Request");
    }

    if (strcmp(path, "/") == 0) {
        strcpy(path, "/index.html");
    }

    if (has_dot_segment(path)) {
        return send_error(client_fd, 403, "Forbidden");
    }

    int n = snprintf(
        candidate, sizeof(candidate), "www%s", path
    );

    if (n < 0 || (size_t)n >= sizeof(candidate)) {
        return send_error(client_fd, 400, "Bad Request");
    }

    if (realpath("www", root_real) == NULL) {
        perror("realpath www");
        return send_error(
            client_fd, 500, "Internal Server Error"
        );
    }

    if (realpath(candidate, file_real) == NULL) {
        return send_error(client_fd, 404, "Not Found");
    }

    /* Ensure the resolved file is within the document root. */
    size_t root_length = strlen(root_real);

    if (strncmp(file_real, root_real, root_length) != 0 ||
        (file_real[root_length] != '/' &&
         file_real[root_length] != '\0')) {
        return send_error(client_fd, 403, "Forbidden");
    }

    int file_fd = open(file_real, O_RDONLY);

    if (file_fd < 0)
        return send_error(client_fd, 404, "Not Found");

    struct stat info;

    if (fstat(file_fd, &info) < 0) {
        close(file_fd);
        return send_error(
            client_fd, 500, "Internal Server Error"
        );
    }

    if (!S_ISREG(info.st_mode)) {
        close(file_fd);
        return send_error(client_fd, 403, "Forbidden");
    }

    char header[512];

    int header_length = snprintf(
        header, sizeof(header),
        "HTTP/1.0 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %lld\r\n"
        "Connection: close\r\n"
        "\r\n",
        get_content_type(path),
        (long long)info.st_size
    );

    if (header_length < 0 ||
        (size_t)header_length >= sizeof(header)) {
        close(file_fd);
        return -1;
    }

    if (send_all(
            client_fd, header, (size_t)header_length) < 0) {
        close(file_fd);
        return -1;
    }

    char buffer[4096];
    ssize_t count;
    int result = 0;

    while ((count = read(file_fd, buffer, sizeof(buffer))) > 0) {
        if (send_all(client_fd, buffer, (size_t)count) < 0) {
            result = -1;
            break;
        }
    }

    if (count < 0) {
        perror("read file");
        result = -1;
    }

    close(file_fd);
    return result;
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
            send_error(client_fd, 400, "Bad Request");
        } else {
            struct http_request request;

            if (http_parse_request(
                    buffer, (size_t)length, &request) < 0) {
                send_error(client_fd, 400, "Bad Request");
            } else if (strcmp(request.method, "GET") != 0) {
                send_error(
                    client_fd, 501, "Not Implemented"
                );
            } else {
                serve_static_file(client_fd, request.uri);
            }
        }

        close(client_fd);
    }

    close(server_fd);
    return 0;
}
