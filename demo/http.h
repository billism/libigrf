/*
 * http.h — tiny blocking HTTP/1.1 server for the libigrf localhost
 * demo.  GET only, one connection at a time, no dependencies beyond
 * the platform socket API.  Not part of the libigrf library.
 */
#ifndef IGRF_DEMO_HTTP_H
#define IGRF_DEMO_HTTP_H

#include <stddef.h>

#ifdef _WIN32
typedef unsigned long long http_socket_t; /* SOCKET */
#define HTTP_INVALID_SOCKET (~0ull)
#else
typedef int http_socket_t;
#define HTTP_INVALID_SOCKET (-1)
#endif

typedef struct {
    char method[8];
    char path[256];     /* percent-decoded path, without query string */
    char query[1024];   /* raw query string (without '?'), may be empty */
} http_request_t;

typedef struct {
    int status;              /* HTTP status code, e.g. 200 */
    const char *content_type;
    char *body;              /* malloc'd buffer, freed by http_serve() */
    size_t len;
} http_response_t;

/*
 * Create the listening socket for port.  Returns the socket handle or
 * HTTP_INVALID_SOCKET on failure.
 */
http_socket_t http_listen(int port);

/*
 * Blocking accept loop.  For every request, cb() is invoked with the
 * parsed request; cb() must fill the response (owning its body).
 * The response body is freed after it has been sent.
 */
void http_serve(http_socket_t listen_fd,
                void (*cb)(const http_request_t *req,
                           http_response_t *resp));

/* Convenience helper: fill resp with a copied body. */
void http_response_set(http_response_t *resp, int status,
                       const char *content_type, const char *body);

/* Look up a query parameter and percent-decode it into out.
 * Returns 1 on success, 0 when the key is absent. */
int http_query_get(const char *query, const char *key, char *out,
                   size_t out_cap);

#endif /* IGRF_DEMO_HTTP_H */
