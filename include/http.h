#ifndef HTTP_H
#define HTTP_H

#include <stddef.h>

#define HTTP_METHOD_MAX  16
#define HTTP_URI_MAX     2048
#define HTTP_VERSION_MAX 16

struct http_request {
    char method[HTTP_METHOD_MAX];
    char uri[HTTP_URI_MAX];
    char version[HTTP_VERSION_MAX];
};

int http_parse_request(
    const char *buffer,
    size_t length,
    struct http_request *request
);

#endif
