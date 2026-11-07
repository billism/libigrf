/*
 * main.c — libigrf localhost web demo.
 *
 * Loads every *.shc model file found in a directory, then serves a
 * small vanilla-JS single-page UI plus a JSON compute endpoint on
 * http://localhost:<port>.  This demo is a library consumer and is
 * intentionally kept outside of the libigrf library target.
 *
 * Usage: igrf_demo [model_dir] [port] [web_dir]
 *   model_dir default: data        (falls back to the directory next
 *                                   to the repository root)
 *   port       default: 8080
 *   web_dir    default: first of demo/web, web that exists
 */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L /* opendir, S_ISDIR, stat */
#endif

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#  include <windows.h>
#else
#  include <dirent.h>
#endif

#include "http.h"
#include "igrf.h"

/* MSVC's C mode does not provide the POSIX S_ISDIR macro. */
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#endif

#define MAX_MODELS 16
#define MAX_MODEL_NAME 64

typedef struct {
    char name[MAX_MODEL_NAME];
    igrf_model_t *model;
} demo_model_t;

static demo_model_t models[MAX_MODELS];
static int n_models = 0;
static char web_dir[512] = "web";

/* ------------------------------------------------------------------ */
/* Model loading                                                       */
/* ------------------------------------------------------------------ */

static void add_model(const char *path, const char *filename)
{
    const char *ext = strrchr(filename, '.');
    size_t base_len;
    demo_model_t *m;

    if (n_models >= MAX_MODELS) {
        return;
    }
    if (ext == NULL || strcmp(ext, ".shc") != 0) {
        return;
    }
    base_len = (size_t)(ext - filename);
    if (base_len == 0 || base_len >= MAX_MODEL_NAME) {
        return;
    }

    m = &models[n_models];
    memcpy(m->name, filename, base_len);
    m->name[base_len] = '\0';
    m->model = igrf_model_load(path);
    if (m->model == NULL) {
        printf("warning: could not load %s\n", path);
        return;
    }
    n_models++;
    printf("loaded model '%s' from %s\n", m->name, path);
}

static int is_directory(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static void load_model_dir(const char *dir)
{
#ifdef _WIN32
    WIN32_FIND_DATAA find;
    char pattern[1024];
    HANDLE h;

    snprintf(pattern, sizeof(pattern), "%s/*", dir);
    h = FindFirstFileA(pattern, &find);
    if (h == INVALID_HANDLE_VALUE) {
        printf("warning: no model files found in '%s'\n", dir);
        return;
    }
    do {
        if (!(find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            char path[1024];
            snprintf(path, sizeof(path), "%s/%s", dir, find.cFileName);
            add_model(path, find.cFileName);
        }
    } while (FindNextFileA(h, &find));
    FindClose(h);
#else
    DIR *d = opendir(dir);
    struct dirent *ent;

    if (d == NULL) {
        printf("warning: no model files found in '%s'\n", dir);
        return;
    }
    while ((ent = readdir(d)) != NULL) {
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
        if (!is_directory(path)) {
            add_model(path, ent->d_name);
        }
    }
    closedir(d);
#endif
}

static demo_model_t *find_model(const char *name)
{
    int i;

    for (i = 0; i < n_models; i++) {
        if (strcmp(models[i].name, name) == 0) {
            return &models[i];
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Static files                                                        */
/* ------------------------------------------------------------------ */

static const char *content_type_for(const char *path)
{
    const char *ext = strrchr(path, '.');

    if (ext == NULL) {
        return "application/octet-stream";
    }
    if (strcmp(ext, ".html") == 0) {
        return "text/html; charset=utf-8";
    }
    if (strcmp(ext, ".css") == 0) {
        return "text/css; charset=utf-8";
    }
    if (strcmp(ext, ".js") == 0) {
        return "application/javascript; charset=utf-8";
    }
    if (strcmp(ext, ".svg") == 0) {
        return "image/svg+xml";
    }
    return "application/octet-stream";
}

static void serve_static(const char *url_path, http_response_t *resp)
{
    char file_path[1024];
    FILE *fp;
    long len;
    char *body;

    if (strstr(url_path, "..") != NULL) {
        http_response_set(resp, 404, "text/plain", "not found\n");
        return;
    }
    snprintf(file_path, sizeof(file_path), "%s%s", web_dir,
             strcmp(url_path, "/") == 0 ? "/index.html" : url_path);

    fp = fopen(file_path, "rb");
    if (fp == NULL) {
        http_response_set(resp, 404, "text/plain", "not found\n");
        return;
    }
    fseek(fp, 0, SEEK_END);
    len = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    body = (char *)malloc((size_t)len + 1);
    if (body == NULL) {
        fclose(fp);
        http_response_set(resp, 500, "text/plain", "out of memory\n");
        return;
    }
    if (fread(body, 1, (size_t)len, fp) != (size_t)len) {
        fclose(fp);
        free(body);
        http_response_set(resp, 500, "text/plain", "read failed\n");
        return;
    }
    body[len] = '\0';
    fclose(fp);

    resp->status = 200;
    resp->content_type = content_type_for(file_path);
    resp->body = body;
    resp->len = (size_t)len;
}

/* ------------------------------------------------------------------ */
/* JSON API                                                            */
/* ------------------------------------------------------------------ */

static int get_double(const char *query, const char *key, double *out)
{
    char buf[64];

    if (!http_query_get(query, key, buf, sizeof(buf))) {
        return 0;
    }
    *out = strtod(buf, NULL);
    return isfinite(*out);
}

static void api_models(http_response_t *resp)
{
    char body[2048];
    size_t off = 0;
    int i;

    off += (size_t)snprintf(body + off, sizeof(body) - off,
                            "{\"ok\":true,\"version\":\"%s\",\"models\":[",
                            igrf_library_version());
    for (i = 0; i < n_models; i++) {
        off += (size_t)snprintf(body + off, sizeof(body) - off,
                                "%s\"%s\"", i > 0 ? "," : "", models[i].name);
    }
    snprintf(body + off, sizeof(body) - off, "]}");
    http_response_set(resp, 200, "application/json", body);
}

static void api_error(http_response_t *resp, int status,
                      const char *message)
{
    char body[512];

    snprintf(body, sizeof(body),
             "{\"ok\":false,\"message\":\"%s\"}", message);
    http_response_set(resp, status, "application/json", body);
}

static void api_compute(const char *query, http_response_t *resp)
{
    char model_name[MAX_MODEL_NAME];
    char coord[32];
    demo_model_t *m;
    double year;

    if (!http_query_get(query, "model", model_name, sizeof(model_name)) ||
        !http_query_get(query, "coord", coord, sizeof(coord)) ||
        !get_double(query, "year", &year)) {
        api_error(resp, 400, "missing or invalid 'model', 'coord' or "
                             "'year' parameter");
        return;
    }
    m = find_model(model_name);
    if (m == NULL) {
        api_error(resp, 400, "unknown model");
        return;
    }

    if (strcmp(coord, "geodetic") == 0) {
        igrf_geodetic_t p;
        igrf_ned_t b;
        double h_nT, f_nT, d_deg, i_deg;
        igrf_status_t st;
        char body[1024];

        if (!get_double(query, "lon", &p.lon_deg) ||
            !get_double(query, "lat", &p.lat_deg) ||
            !get_double(query, "height", &p.height_km)) {
            api_error(resp, 400, "invalid geodetic parameters");
            return;
        }
        st = igrf_compute_geodetic(m->model, year, &p, &b);
        if (st != IGRF_OK) {
            api_error(resp, 400,
                      st == IGRF_ERROR_YEAR_OUT_OF_RANGE
                          ? "year outside the model's epoch range"
                          : "invalid input");
            return;
        }
        igrf_ned_elements(&b, &h_nT, &f_nT, &d_deg, &i_deg);
        snprintf(body, sizeof(body),
                 "{\"ok\":true,\"geodetic\":true,\"model\":\"%s\","
                 "\"year\":%.17g,"
                 "\"lon\":%.17g,\"lat\":%.17g,\"height\":%.17g,"
                 "\"X\":%.17g,\"Y\":%.17g,\"Z\":%.17g,"
                 "\"H\":%.17g,\"F\":%.17g,\"D\":%.17g,\"I\":%.17g}",
                 m->name, year, p.lon_deg, p.lat_deg, p.height_km,
                 b.X_nT, b.Y_nT, b.Z_nT, h_nT, f_nT, d_deg, i_deg);
        http_response_set(resp, 200, "application/json", body);
    } else if (strcmp(coord, "geocentric") == 0) {
        igrf_geocentric_t p;
        igrf_spherical_t b;
        igrf_status_t st;
        double f_nT;
        char body[1024];

        if (!get_double(query, "r", &p.r_km) ||
            !get_double(query, "theta", &p.theta_deg) ||
            !get_double(query, "phi", &p.phi_deg)) {
            api_error(resp, 400, "invalid geocentric parameters");
            return;
        }
        st = igrf_compute_geocentric(m->model, year, &p, &b);
        if (st != IGRF_OK) {
            api_error(resp, 400,
                      st == IGRF_ERROR_YEAR_OUT_OF_RANGE
                          ? "year outside the model's epoch range"
                          : "invalid input");
            return;
        }
        f_nT = sqrt(b.Br_nT * b.Br_nT + b.Btheta_nT * b.Btheta_nT +
                    b.Bphi_nT * b.Bphi_nT);
        snprintf(body, sizeof(body),
                 "{\"ok\":true,\"geodetic\":false,\"model\":\"%s\","
                 "\"year\":%.17g,"
                 "\"r\":%.17g,\"theta\":%.17g,\"phi\":%.17g,"
                 "\"Br\":%.17g,\"Btheta\":%.17g,\"Bphi\":%.17g,"
                 "\"F\":%.17g}",
                 m->name, year, p.r_km, p.theta_deg, p.phi_deg,
                 b.Br_nT, b.Btheta_nT, b.Bphi_nT, f_nT);
        http_response_set(resp, 200, "application/json", body);
    } else {
        api_error(resp, 400, "coord must be 'geodetic' or 'geocentric'");
    }
}

/* ------------------------------------------------------------------ */
/* Request dispatch                                                    */
/* ------------------------------------------------------------------ */

static void handle_request(const http_request_t *req, http_response_t *resp)
{
    if (strcmp(req->path, "/api/models") == 0) {
        api_models(resp);
    } else if (strcmp(req->path, "/api/compute") == 0) {
        api_compute(req->query, resp);
    } else {
        serve_static(req->path, resp);
    }
}

/* ------------------------------------------------------------------ */
/* Entry point                                                         */
/* ------------------------------------------------------------------ */

int main(int argc, char **argv)
{
    const char *model_dir = argc > 1 ? argv[1] : "data";
    int port = argc > 2 ? atoi(argv[2]) : 8080;
    const char *web_dir_arg = argc > 3 ? argv[3] : NULL;
    http_socket_t listen_fd;

    setvbuf(stdout, NULL, _IONBF, 0); /* live logs when redirected */

    if (web_dir_arg != NULL) {
        snprintf(web_dir, sizeof(web_dir), "%s", web_dir_arg);
    } else if (is_directory("demo/web")) {
        snprintf(web_dir, sizeof(web_dir), "demo/web");
    }

    load_model_dir(model_dir);
    if (n_models == 0) {
        fprintf(stderr,
                "error: no .shc model files could be loaded from '%s'\n"
                "usage: %s [model_dir] [port] [web_dir]\n",
                model_dir, argv[0]);
        return EXIT_FAILURE;
    }

    listen_fd = http_listen(port);
    if (listen_fd == HTTP_INVALID_SOCKET) {
        fprintf(stderr, "error: could not listen on port %d\n", port);
        return EXIT_FAILURE;
    }

    printf("\nlibigrf %s demo serving %d model(s) from '%s'\n",
           igrf_library_version(), n_models, model_dir);
    printf("open http://localhost:%d/  (web dir: '%s', Ctrl+C to stop)\n\n",
           port, web_dir);
    http_serve(listen_fd, handle_request);
    return EXIT_SUCCESS;
}
