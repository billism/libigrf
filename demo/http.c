/*
 * http.c — minimal blocking HTTP/1.1 server for the libigrf demo.
 * Uses Winsock2 on Windows and POSIX sockets elsewhere; only GET
 * requests are understood and connections are handled one at a time.
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "http.h"

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  define http_close_socket closesocket
#else
#  include <arpa/inet.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#  include <unistd.h>
#  define http_close_socket close
#endif

static void http_platform_init(void)
{
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
}

http_socket_t http_listen(int port)
{
    http_socket_t fd;
    struct sockaddr_in addr;
    int one = 1;

    http_platform_init();

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == HTTP_INVALID_SOCKET) {
        return HTTP_INVALID_SOCKET;
    }
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&one,
               sizeof(one));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); /* localhost only */
    addr.sin_port = htons((unsigned short)port);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
        listen(fd, 4) != 0) {
        http_close_socket(fd);
        return HTTP_INVALID_SOCKET;
    }
    return fd;
}

static void http_percent_decode(const char *src, char *dst, size_t cap)
{
    size_t o = 0;

    while (*src != '\0' && o + 1 < cap) {
        if (*src == '%' && isxdigit((unsigned char)src[1]) &&
            isxdigit((unsigned char)src[2])) {
            char hex[3] = { src[1], src[2], '\0' };
            dst[o++] = (char)strtol(hex, NULL, 16);
            src += 3;
        } else if (*src == '+') {
            dst[o++] = ' ';
            src++;
        } else {
            dst[o++] = *src++;
        }
    }
    dst[o] = '\0';
}

int http_query_get(const char *query, const char *key, char *out,
                   size_t out_cap)
{
    size_t key_len = strlen(key);
    const char *p = query;

    while (p != NULL && *p != '\0') {
        const char *sep = strchr(p, '&');
        size_t seg_len = sep != NULL ? (size_t)(sep - p) : strlen(p);

        if (seg_len > key_len && strncmp(p, key, key_len) == 0 &&
            p[key_len] == '=') {
            char raw[256];
            size_t raw_len = seg_len - key_len - 1;

            if (raw_len >= sizeof(raw)) {
                raw_len = sizeof(raw) - 1;
            }
            memcpy(raw, p + key_len + 1, raw_len);
            raw[raw_len] = '\0';
            http_percent_decode(raw, out, out_cap);
            return 1;
        }
        p = sep != NULL ? sep + 1 : NULL;
    }
    return 0;
}

void http_response_set(http_response_t *resp, int status,
                       const char *content_type, const char *body)
{
    size_t len = strlen(body);

    resp->status = status;
    resp->content_type = content_type;
    resp->body = (char *)malloc(len + 1);
    resp->len = 0;
    if (resp->body == NULL) {
        return;
    }
    memcpy(resp->body, body, len + 1);
    resp->len = len;
}

static void http_send_all(http_socket_t fd, const char *data, size_t len)
{
    size_t off = 0;

    while (off < len) {
        int chunk = (len - off > 65536) ? 65536 : (int)(len - off);
        int sent = send(fd, data + off, chunk, 0);
        if (sent <= 0) {
            return;
        }
        off += (size_t)sent;
    }
}

static const char *http_status_text(int status)
{
    switch (status) {
    case 200: return "OK";
    case 400: return "Bad Request";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 500: return "Internal Server Error";
    default: return "OK";
    }
}

void http_serve(http_socket_t listen_fd,
                void (*cb)(const http_request_t *req,
                           http_response_t *resp))
{
    for (;;) {
        http_socket_t conn = accept(listen_fd, NULL, NULL);
        char buf[8192];
        size_t have = 0;
        http_request_t req;
        http_response_t resp;
        char status_line[512];

        if (conn == HTTP_INVALID_SOCKET) {
            continue;
        }

        /* read until end of request headers or buffer is full */
        while (have < sizeof(buf) - 1) {
            int got = recv(conn, buf + have, (int)(sizeof(buf) - 1 - have),
                           0);
            if (got <= 0) {
                break;
            }
            have += (size_t)got;
            buf[have] = '\0';
            if (strstr(buf, "\r\n\r\n") != NULL ||
                strstr(buf, "\n\n") != NULL) {
                break;
            }
        }
        buf[have] = '\0';

        /* parse the request line: METHOD SP TARGET SP VERSION */
        memset(&req, 0, sizeof(req));
        {
            char target[1024] = "";
            if (sscanf(buf, "%7s %1023s", req.method, target) != 2) {
                http_response_set(&resp, 400, "text/plain",
                                  "bad request\n");
            } else if (strcmp(req.method, "GET") != 0) {
                http_response_set(&resp, 405, "text/plain",
                                  "only GET is supported\n");
            } else {
                char *qm = strchr(target, '?');
                char raw_path[512];

                if (qm != NULL) {
                    size_t n = (size_t)(qm - target);
                    if (n >= sizeof(raw_path)) {
                        n = sizeof(raw_path) - 1;
                    }
                    memcpy(raw_path, target, n);
                    raw_path[n] = '\0';
                    snprintf(req.query, sizeof(req.query), "%s", qm + 1);
                } else {
                    snprintf(raw_path, sizeof(raw_path), "%s", target);
                }
                http_percent_decode(raw_path, req.path, sizeof(req.path));
                cb(&req, &resp);
            }
        }

        if (resp.body == NULL) {
            http_response_set(&resp, 500, "text/plain",
                              "response allocation failed\n");
        }

        snprintf(status_line, sizeof(status_line),
                 "HTTP/1.1 %d %s\r\n"
                 "Content-Type: %s\r\n"
                 "Content-Length: %llu\r\n"
                 "Cache-Control: no-store\r\n"
                 "Connection: close\r\n"
                 "\r\n",
                 resp.status, http_status_text(resp.status),
                 resp.content_type, (unsigned long long)resp.len);
        http_send_all(conn, status_line, strlen(status_line));
        http_send_all(conn, resp.body, resp.len);

        free(resp.body);
        resp.body = NULL;

        /* Graceful close: signal end of response, drain whatever the
         * peer still sends, then close.  Closing directly can send a
         * TCP reset on Windows (unread receive data), which truncates
         * responses for the browser. */
        {
            char drain[1024];

#ifdef _WIN32
            DWORD rcv_timeout_ms = 500;
            shutdown(conn, SD_SEND);
            setsockopt(conn, SOL_SOCKET, SO_RCVTIMEO,
                       (const char *)&rcv_timeout_ms, sizeof(rcv_timeout_ms));
#else
            struct timeval tv = { 0, 500 * 1000 };
            shutdown(conn, SHUT_WR);
            setsockopt(conn, SOL_SOCKET, SO_RCVTIMEO,
                       (const char *)&tv, sizeof(tv));
#endif
            while (recv(conn, drain, sizeof(drain), 0) > 0) {
                /* discard */
            }
        }
        http_close_socket(conn);
    }
}
