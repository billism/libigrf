/*
 * test_batch.c — batch computation consistency tests.
 *
 * Batch results must equal single-point results exactly (the same
 * internal code path is used, so only identical floating point
 * behaviour is acceptable), including for varying-time batches whose
 * times repeat non-monotonically.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "igrf.h"
#include "testutil.h"

/* Deterministic pseudo-random generator so failures are reproducible. */
static unsigned long long rng_state = 0x9E3779B97F4A7C15ull;

static double rng_uniform(void)
{
    rng_state = rng_state * 6364136223846793005ull + 1442695040888963407ull;
    return (double)(rng_state >> 11) / 9007199254740992.0; /* [0, 1) */
}

#define N_POINTS 257

int main(void)
{
    igrf_model_t *m = igrf_model_load(IGRF_TEST_DATA_DIR "/igrf14.shc");
    static igrf_geodetic_t gd[N_POINTS];
    static igrf_ned_t ned_batch[N_POINTS];
    static igrf_ned_t ned_single[N_POINTS];
    static igrf_geocentric_t gc[N_POINTS];
    static igrf_spherical_t sph_batch[N_POINTS];
    static igrf_spherical_t sph_single[N_POINTS];
    static igrf_geodetic_time_point_t gdt[N_POINTS * 2];
    static igrf_ned_t nedt_batch[N_POINTS * 2];
    static igrf_ned_t nedt_single[N_POINTS * 2];
    static igrf_geocentric_time_point_t gct[N_POINTS * 2];
    static igrf_spherical_t spht_batch[N_POINTS * 2];
    static igrf_spherical_t spht_single[N_POINTS * 2];
    const double year = 2023.75;
    size_t i;
    int k;

    CHECK(m != NULL, "model must load");

    /* --- deterministic global grid --- */
    for (i = 0; i < N_POINTS; i++) {
        gd[i].lon_deg = rng_uniform() * 360.0 - 180.0;
        gd[i].lat_deg = rng_uniform() * 180.0 - 90.0;
        gd[i].height_km = rng_uniform() * 1000.0;
        gc[i].r_km = 6371.2 + rng_uniform() * 1000.0;
        gc[i].theta_deg = rng_uniform() * 180.0;
        gc[i].phi_deg = rng_uniform() * 360.0 - 180.0;
    }

    /* --- fixed-time geodetic batch == single-point loop --- */
    CHECK_STATUS(igrf_compute_geodetic_batch(m, year, gd, N_POINTS,
                                             ned_batch),
                 IGRF_OK);
    for (i = 0; i < N_POINTS; i++) {
        CHECK_STATUS(igrf_compute_geodetic(m, year, &gd[i],
                                           &ned_single[i]),
                     IGRF_OK);
        CHECK_CLOSE(ned_batch[i].X_nT, ned_single[i].X_nT, 1e-9,
                    "geodetic batch X");
        CHECK_CLOSE(ned_batch[i].Y_nT, ned_single[i].Y_nT, 1e-9,
                    "geodetic batch Y");
        CHECK_CLOSE(ned_batch[i].Z_nT, ned_single[i].Z_nT, 1e-9,
                    "geodetic batch Z");
    }

    /* --- fixed-time geocentric batch == single-point loop --- */
    CHECK_STATUS(igrf_compute_geocentric_batch(m, year, gc, N_POINTS,
                                               sph_batch),
                 IGRF_OK);
    for (i = 0; i < N_POINTS; i++) {
        CHECK_STATUS(igrf_compute_geocentric(m, year, &gc[i],
                                             &sph_single[i]),
                     IGRF_OK);
        CHECK_CLOSE(sph_batch[i].Br_nT, sph_single[i].Br_nT, 1e-9,
                    "geocentric batch Br");
        CHECK_CLOSE(sph_batch[i].Btheta_nT, sph_single[i].Btheta_nT, 1e-9,
                    "geocentric batch Btheta");
        CHECK_CLOSE(sph_batch[i].Bphi_nT, sph_single[i].Bphi_nT, 1e-9,
                    "geocentric batch Bphi");
    }

    /* --- varying-time batches, with non-monotonic repeated years --- */
    for (i = 0; i < N_POINTS * 2; i++) {
        /* the second half repeats year patterns to exercise caching */
        double years[7] = { 1902.5, 2000.0, 2021.25, 1950.0,
                            2000.0, 2029.75, 2021.25 };
        gdt[i].decimal_year = years[(2 * i) % 7];
        gdt[i].lon_deg = gd[i % N_POINTS].lon_deg;
        gdt[i].lat_deg = gd[i % N_POINTS].lat_deg;
        gdt[i].height_km = gd[i % N_POINTS].height_km;
        gct[i].decimal_year = years[(2 * i + 1) % 7];
        gct[i].r_km = gc[i % N_POINTS].r_km;
        gct[i].theta_deg = gc[i % N_POINTS].theta_deg;
        gct[i].phi_deg = gc[i % N_POINTS].phi_deg;
    }

    CHECK_STATUS(igrf_compute_geodetic_batch_varying_time(
                     m, gdt, N_POINTS * 2, nedt_batch),
                 IGRF_OK);
    CHECK_STATUS(igrf_compute_geocentric_batch_varying_time(
                     m, gct, N_POINTS * 2, spht_batch),
                 IGRF_OK);
    for (i = 0; i < N_POINTS * 2; i++) {
        igrf_geodetic_t p;

        p.lon_deg = gdt[i].lon_deg;
        p.lat_deg = gdt[i].lat_deg;
        p.height_km = gdt[i].height_km;
        CHECK_STATUS(igrf_compute_geodetic(m, gdt[i].decimal_year, &p,
                                           &nedt_single[i]),
                     IGRF_OK);
        CHECK_CLOSE(nedt_batch[i].X_nT, nedt_single[i].X_nT, 1e-9,
                    "varying geodetic batch X");
        CHECK_CLOSE(nedt_batch[i].Z_nT, nedt_single[i].Z_nT, 1e-9,
                    "varying geodetic batch Z");

        {
            igrf_geocentric_t p;

            p.r_km = gct[i].r_km;
            p.theta_deg = gct[i].theta_deg;
            p.phi_deg = gct[i].phi_deg;
            CHECK_STATUS(igrf_compute_geocentric(m, gct[i].decimal_year,
                                                 &p, &spht_single[i]),
                         IGRF_OK);
        }
        CHECK_CLOSE(spht_batch[i].Br_nT, spht_single[i].Br_nT, 1e-9,
                    "varying geocentric batch Br");
        CHECK_CLOSE(spht_batch[i].Bphi_nT, spht_single[i].Bphi_nT, 1e-9,
                    "varying geocentric batch Bphi");
    }

    /* --- argument validation --- */
    CHECK_STATUS(igrf_compute_geodetic_batch(NULL, year, gd, N_POINTS,
                                             ned_batch),
                 IGRF_ERROR_NULL_ARGUMENT);
    CHECK_STATUS(igrf_compute_geodetic_batch(m, year, NULL, N_POINTS,
                                             ned_batch),
                 IGRF_ERROR_NULL_ARGUMENT);
    CHECK_STATUS(igrf_compute_geodetic_batch(m, year, gd, N_POINTS, NULL),
                 IGRF_ERROR_NULL_ARGUMENT);
    CHECK_STATUS(igrf_compute_geodetic_batch(m, year, gd, 0, ned_batch),
                 IGRF_ERROR_INVALID_INPUT);
    CHECK_STATUS(igrf_compute_geocentric_batch(m, year, gc, 0, sph_batch),
                 IGRF_ERROR_INVALID_INPUT);
    CHECK_STATUS(igrf_compute_geodetic_batch_varying_time(m, gdt, 0,
                                                          nedt_batch),
                 IGRF_ERROR_INVALID_INPUT);
    CHECK_STATUS(igrf_compute_geocentric_batch_varying_time(m, gct, 0,
                                                            spht_batch),
                 IGRF_ERROR_INVALID_INPUT);

    /* one invalid point poisons the whole batch (all-or-nothing) */
    gd[10].lat_deg = 95.0;
    CHECK_STATUS(igrf_compute_geodetic_batch(m, year, gd, N_POINTS,
                                             ned_batch),
                 IGRF_ERROR_INVALID_INPUT);
    gd[10].lat_deg = 0.0;

    /* out-of-range years in varying-time batches */
    gdt[3].decimal_year = 2031.0;
    CHECK_STATUS(igrf_compute_geodetic_batch_varying_time(
                     m, gdt, N_POINTS * 2, nedt_batch),
                 IGRF_ERROR_YEAR_OUT_OF_RANGE);
    gdt[3].decimal_year = 1899.0;
    CHECK_STATUS(igrf_compute_geodetic_batch_varying_time(
                     m, gdt, N_POINTS * 2, nedt_batch),
                 IGRF_ERROR_YEAR_OUT_OF_RANGE);
    gct[5].decimal_year = 2031.0;
    CHECK_STATUS(igrf_compute_geocentric_batch_varying_time(
                     m, gct, N_POINTS * 2, spht_batch),
                 IGRF_ERROR_YEAR_OUT_OF_RANGE);

    (void)k;
    igrf_model_free(m);
    return tt_report("test_batch");
}
