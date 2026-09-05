/*
 * benchmark.c — performance measurement for libigrf.
 *
 * Measures, on identical deterministic input:
 *   - model loading (.shc parse)
 *   - single-point API calls (geodetic / geocentric), one model
 *     preparation + workspace allocation per call
 *   - fixed-time batch API (coefficients prepared once)
 *   - varying-time batch API (a preparation per distinct year)
 *   - varying-time batch with a constant year (caching path)
 *
 * Best-of-5 timings; a checksum of all field values is accumulated
 * and printed so that no run can be optimized away.
 *
 * Usage: igrf_benchmark [model.shc] [n_points]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "igrf.h"

#ifdef _WIN32
#  include <windows.h>
static double bench_now_seconds(void)
{
    LARGE_INTEGER freq, counter;

    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart / (double)freq.QuadPart;
}
#else
#  include <time.h>
static double bench_now_seconds(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}
#endif

/* Deterministic pseudo-random generator (same style as the tests). */
static unsigned long long rng_state = 0x9E3779B97F4A7C15ull;

static double rng_uniform(void)
{
    rng_state = rng_state * 6364136223846793005ull + 1442695040888963407ull;
    return (double)(rng_state >> 11) / 9007199254740992.0; /* [0, 1) */
}

#define REPETITIONS 5

static const char *bench_model_file = "data/igrf14.shc";

typedef double (*bench_fn)(const igrf_model_t *model, size_t n,
                           const void *input, void *output);

static void run_scenario(const char *name, const igrf_model_t *model,
                         size_t n, const void *input, void *output,
                         bench_fn fn, int checksum_outputs)
{
    double best = 0.0;
    double checksum = 0.0;
    int rep;
    const double *values = (const double *)output;
    size_t k;

    /* warmup (page faults, allocator, caches) */
    checksum += fn(model, n, input, output);

    for (rep = 0; rep < REPETITIONS; rep++) {
        double start = bench_now_seconds();
        double elapsed;

        checksum += fn(model, n, input, output);
        elapsed = bench_now_seconds() - start;
        if (rep == 0 || elapsed < best) {
            best = elapsed;
        }
    }

    /* reading every output value keeps the runs from being elided */
    if (checksum_outputs) {
        for (k = 0; k < n * 3; k++) {
            checksum += values[k];
        }
    }

    printf("  %-44s %9.4f s  %9.3f us/pt  %13.0f pts/s  (chk %.7g)\n",
           name, best, best * 1e6 / (double)n, (double)n / best, checksum);
}

/* ----------------------------- scenarios ----------------------------- */

static double bench_load(const igrf_model_t *model, size_t n,
                         const void *input, void *output)
{
    igrf_model_t *reloaded;
    double t0 = bench_now_seconds();

    reloaded = igrf_model_load(bench_model_file);
    if (reloaded == NULL) {
        return -1.0;
    }
    igrf_model_free(reloaded);
    (void)model;
    (void)n;
    (void)input;
    (void)output;
    return bench_now_seconds() - t0;
}

static double bench_geodetic_single(const igrf_model_t *model, size_t n,
                                    const void *input, void *output)
{
    const igrf_geodetic_t *positions = (const igrf_geodetic_t *)input;
    igrf_ned_t *fields = (igrf_ned_t *)output;
    size_t i;

    for (i = 0; i < n; i++) {
        igrf_compute_geodetic(model, 2023.75, &positions[i], &fields[i]);
    }
    return 0.0;
}

static double bench_geocentric_single(const igrf_model_t *model, size_t n,
                                      const void *input, void *output)
{
    const igrf_geocentric_t *positions = (const igrf_geocentric_t *)input;
    igrf_spherical_t *fields = (igrf_spherical_t *)output;
    size_t i;

    for (i = 0; i < n; i++) {
        igrf_compute_geocentric(model, 2023.75, &positions[i], &fields[i]);
    }
    return 0.0;
}

static double bench_geodetic_batch(const igrf_model_t *model, size_t n,
                                   const void *input, void *output)
{
    return igrf_compute_geodetic_batch(model, 2023.75,
                                       (const igrf_geodetic_t *)input, n,
                                       (igrf_ned_t *)output) == IGRF_OK
               ? 0.0
               : -1.0;
}

static double bench_geocentric_batch(const igrf_model_t *model, size_t n,
                                     const void *input, void *output)
{
    return igrf_compute_geocentric_batch(model, 2023.75,
                                         (const igrf_geocentric_t *)input, n,
                                         (igrf_spherical_t *)output) ==
                   IGRF_OK
               ? 0.0
               : -1.0;
}

/* Each point carries its own decimal year, spread over the model. */
static double bench_geodetic_varying(const igrf_model_t *model, size_t n,
                                     const void *input, void *output)
{
    const igrf_geodetic_time_point_t *points =
        (const igrf_geodetic_time_point_t *)input;
    igrf_ned_t *fields = (igrf_ned_t *)output;

    return igrf_compute_geodetic_batch_varying_time(model, points, n,
                                                    fields) == IGRF_OK
               ? 0.0
               : -1.0;
}

static double bench_geocentric_varying(const igrf_model_t *model, size_t n,
                                       const void *input, void *output)
{
    const igrf_geocentric_time_point_t *points =
        (const igrf_geocentric_time_point_t *)input;
    igrf_spherical_t *fields = (igrf_spherical_t *)output;

    return igrf_compute_geocentric_batch_varying_time(model, points, n,
                                                      fields) == IGRF_OK
               ? 0.0
               : -1.0;
}

/* Varying-time API with a constant year: measures the year-caching
 * path (one preparation for the whole batch). */
static double bench_geodetic_varying_same_year(const igrf_model_t *model,
                                               size_t n, const void *input,
                                               void *output)
{
    const igrf_geodetic_t *positions = (const igrf_geodetic_t *)input;
    igrf_ned_t *fields = (igrf_ned_t *)output;
    igrf_geodetic_time_point_t *points;
    size_t i;
    double result;

    points = (igrf_geodetic_time_point_t *)malloc(n * sizeof(*points));
    if (points == NULL) {
        return -1.0;
    }
    for (i = 0; i < n; i++) {
        points[i].decimal_year = 2023.75;
        points[i].lon_deg = positions[i].lon_deg;
        points[i].lat_deg = positions[i].lat_deg;
        points[i].height_km = positions[i].height_km;
    }
    result = igrf_compute_geodetic_batch_varying_time(model, points, n,
                                                      fields) == IGRF_OK
                 ? 0.0
                 : -1.0;
    free(points);
    return result;
}

int main(int argc, char **argv)
{
    const char *model_file = argc > 1 ? argv[1] : "data/igrf14.shc";
    size_t n = argc > 2 ? (size_t)strtoul(argv[2], NULL, 10) : 100000;
    igrf_model_t *model;
    igrf_geodetic_t *gd;
    igrf_geocentric_t *gc;
    igrf_geodetic_time_point_t *gdt;
    igrf_geocentric_time_point_t *gct;
    igrf_ned_t *ned;
    igrf_spherical_t *sph;
    size_t i;

    bench_model_file = model_file;
    model = igrf_model_load(model_file);
    if (model == NULL) {
        fprintf(stderr, "error: could not load '%s'\n", model_file);
        return EXIT_FAILURE;
    }

    gd = (igrf_geodetic_t *)malloc(n * sizeof(*gd));
    gc = (igrf_geocentric_t *)malloc(n * sizeof(*gc));
    gdt = (igrf_geodetic_time_point_t *)malloc(n * sizeof(*gdt));
    gct = (igrf_geocentric_time_point_t *)malloc(n * sizeof(*gct));
    ned = (igrf_ned_t *)malloc(n * sizeof(*ned));
    sph = (igrf_spherical_t *)malloc(n * sizeof(*sph));
    if (gd == NULL || gc == NULL || gdt == NULL || gct == NULL ||
        ned == NULL || sph == NULL) {
        fprintf(stderr, "error: out of memory\n");
        return EXIT_FAILURE;
    }

    for (i = 0; i < n; i++) {
        double year = 1900.5 + rng_uniform() * 129.0; /* 1900.5..2029.5 */

        gd[i].lon_deg = rng_uniform() * 360.0 - 180.0;
        gd[i].lat_deg = rng_uniform() * 178.0 - 89.0;
        gd[i].height_km = rng_uniform() * 1000.0;
        gc[i].r_km = 6371.2 + rng_uniform() * 1000.0;
        gc[i].theta_deg = rng_uniform() * 180.0;
        gc[i].phi_deg = rng_uniform() * 360.0 - 180.0;
        gdt[i].decimal_year = year;
        gdt[i].lon_deg = gd[i].lon_deg;
        gdt[i].lat_deg = gd[i].lat_deg;
        gdt[i].height_km = gd[i].height_km;
        gct[i].decimal_year = year;
        gct[i].r_km = gc[i].r_km;
        gct[i].theta_deg = gc[i].theta_deg;
        gct[i].phi_deg = gc[i].phi_deg;
    }

    printf("libigrf %s benchmark — %s, %llu points, best of %d\n\n",
           igrf_library_version(), model_file,
           (unsigned long long)n, REPETITIONS);

    run_scenario("model load (.shc parse)", model, 1, NULL, NULL,
                 bench_load, 0);
    printf("\n");

    run_scenario("single-point geodetic (N calls)", model, n, gd, ned,
                 bench_geodetic_single, 1);
    run_scenario("single-point geocentric (N calls)", model, n, gc, sph,
                 bench_geocentric_single, 1);
    run_scenario("fixed-time batch geodetic", model, n, gd, ned,
                 bench_geodetic_batch, 1);
    run_scenario("fixed-time batch geocentric", model, n, gc, sph,
                 bench_geocentric_batch, 1);
    run_scenario("varying-time batch geodetic (distinct years)", model, n,
                 gdt, ned, bench_geodetic_varying, 1);
    run_scenario("varying-time batch geocentric (distinct years)", model, n,
                 gct, sph, bench_geocentric_varying, 1);
    run_scenario("varying-time batch geodetic (same year)", model, n, gd,
                 ned, bench_geodetic_varying_same_year, 1);

    free(gd);
    free(gc);
    free(gdt);
    free(gct);
    free(ned);
    free(sph);
    igrf_model_free(model);
    return EXIT_SUCCESS;
}
