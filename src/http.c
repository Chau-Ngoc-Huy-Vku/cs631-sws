#include "http.h"

#include <string.h>

int
http_parse_request(
    const char *buffer,
    size_t length,
    struct http_request *request)
{
    size_t line_end = 0;
    size_t first_space = 0;
    size_t second_space = 0;
    size_t method_len;
    size_t uri_len;
    size_t version_len;
    int found_line_end = 0;
    int found_first_space = 0;
    int found_second_space = 0;

    if (buffer == NULL || request == NULL)
        return -1;

    /* Find the end of the HTTP request line. */
    for (size_t i = 0; i + 1 < length; i++) {
        if (buffer[i] == '\r' && buffer[i + 1] == '\n') {
            line_end = i;
            found_line_end = 1;
            break;
        }
    }

    if (!found_line_end || line_end == 0)
        return -1;

    /* Find the two spaces separating method, URI and version. */
    for (size_t i = 0; i < line_end; i++) {
        if (buffer[i] == ' ') {
            if (!found_first_space) {
                first_space = i;
                found_first_space = 1;
            } else if (!found_second_space) {
                second_space = i;
                found_second_space = 1;
            } else {
                return -1;
            }
        } else if ((unsigned char)buffer[i] < 0x21 ||
                   (unsigned char)buffer[i] > 0x7e) {
            return -1;
        }
    }

    if (!found_first_space || !found_second_space)
        return -1;

    method_len = first_space;
    uri_len = second_space - first_space - 1;
    version_len = line_end - second_space - 1;

    if (method_len == 0 || uri_len == 0 || version_len == 0)
        return -1;

    if (method_len >= sizeof(request->method) ||
        uri_len >= sizeof(request->uri) ||
        version_len >= sizeof(request->version))
        return -1;

    /* This prototype accepts origin-style paths such as /index.html. */
    if (buffer[first_space + 1] != '/')
        return -1;

    memcpy(request->method, buffer, method_len);
    request->method[method_len] = '\0';

    memcpy(request->uri,
           buffer + first_space + 1,
           uri_len);
    request->uri[uri_len] = '\0';

    memcpy(request->version,
           buffer + second_space + 1,
           version_len);
    request->version[version_len] = '\0';

    /* Accept HTTP/1.0 and HTTP/1.1 requests. */
    if (strcmp(request->version, "HTTP/1.0") != 0 &&
        strcmp(request->version, "HTTP/1.1") != 0)
        return -1;

    return 0;
}
