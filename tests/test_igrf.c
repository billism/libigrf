/*
 * test_igrf.c — single-point IGRF computation unit tests: year range
 * enforcement, time continuity, pole handling, physical sanity and
 * input validation.
 */
#include <math.h>
#include <stdio.h>

#include "igrf.h"
#include "testutil.h"

static double field_F_geodetic(const igrf_ned_t *b)
{
    return sqrt(b->X_nT * b->X_nT + b->Y_nT * b->Y_nT + b->Z_nT * b->Z_nT);
}

static double field_F_spherical(const igrf_spherical_t *b)
{
    return sqrt(b->Br_nT * b->Br_nT + b->Btheta_nT * b->Btheta_nT +
                b->Bphi_nT * b->Bphi_nT);
}

int main(void)
{
    igrf_model_t *m = igrf_model_load(IGRF_TEST_DATA_DIR "/igrf14.shc");
    igrf_ned_t ned;
    igrf_spherical_t sph;
    igrf_geodetic_t gd;
    igrf_geocentric_t gc;
    igrf_ned_t ned2;
    igrf_spherical_t sph2;

    CHECK(m != NULL, "model must load");

    /* --- year range is enforced without extrapolation --- */
    gd.lon_deg = 0.0;
    gd.lat_deg = 0.0;
    gd.height_km = 0.0;
    gc.r_km = 6371.2;
    gc.theta_deg = 90.0;
    gc.phi_deg = 0.0;
    CHECK_STATUS(igrf_compute_geodetic(m, 1899.9999, &gd, &ned),
                 IGRF_ERROR_YEAR_OUT_OF_RANGE);
    CHECK_STATUS(igrf_compute_geodetic(m, 2030.0000001, &gd, &ned),
                 IGRF_ERROR_YEAR_OUT_OF_RANGE);
    CHECK_STATUS(igrf_compute_geocentric(m, 1899.9999, &gc, &sph),
                 IGRF_ERROR_YEAR_OUT_OF_RANGE);
    CHECK_STATUS(igrf_compute_geocentric(m, 2030.0000001, &gc, &sph),
                 IGRF_ERROR_YEAR_OUT_OF_RANGE);

    /* epoch endpoints themselves are valid */
    CHECK_STATUS(igrf_compute_geodetic(m, 1900.0, &gd, &ned), IGRF_OK);
    CHECK_STATUS(igrf_compute_geodetic(m, 2030.0, &gd, &ned), IGRF_OK);

    /* --- interpolation continuity --- */
    {
        /* across the 2025 epoch knot: 2020-2025 and 2025-2030
         * segments meet there */
        igrf_ned_t a, b, c;
        igrf_compute_geodetic(m, 2024.9999999, &gd, &a);
        igrf_compute_geodetic(m, 2025.0, &gd, &b);
        igrf_compute_geodetic(m, 2025.0000001, &gd, &c);
        CHECK_CLOSE(a.X_nT, b.X_nT, 1e-4, "X continuity at 2025 (left)");
        CHECK_CLOSE(c.X_nT, b.X_nT, 1e-4, "X continuity at 2025 (right)");
        CHECK_CLOSE(a.Z_nT, b.Z_nT, 1e-4, "Z continuity at 2025 (left)");
        CHECK_CLOSE(c.Z_nT, b.Z_nT, 1e-4, "Z continuity at 2025 (right)");

        /* inside a segment: interpolation is smooth */
        igrf_compute_geodetic(m, 2022.4999999, &gd, &a);
        igrf_compute_geodetic(m, 2022.5000001, &gd, &c);
        CHECK_CLOSE(a.Y_nT, c.Y_nT, 1e-4, "Y continuity at 2022.5");
    }

    /* --- geocentric poles produce finite, continuous fields --- */
    gc.r_km = 6371.2;
    gc.phi_deg = 123.0;
    gc.theta_deg = 0.0;
    CHECK_STATUS(igrf_compute_geocentric(m, 2025.0, &gc, &sph), IGRF_OK);
    CHECK(isfinite(sph.Br_nT) && isfinite(sph.Btheta_nT) &&
              isfinite(sph.Bphi_nT),
          "north pole field is finite");
    gc.theta_deg = 1e-6;
    CHECK_STATUS(igrf_compute_geocentric(m, 2025.0, &gc, &sph2), IGRF_OK);
    CHECK_CLOSE(sph.Br_nT, sph2.Br_nT, 1e-2, "Br continuity at north pole");
    CHECK_CLOSE(sph.Btheta_nT, sph2.Btheta_nT, 1e-2,
                "Btheta continuity at north pole");
    CHECK_CLOSE(sph.Bphi_nT, sph2.Bphi_nT, 1e-2,
                "Bphi continuity at north pole");

    gc.theta_deg = 180.0;
    CHECK_STATUS(igrf_compute_geocentric(m, 2025.0, &gc, &sph), IGRF_OK);
    CHECK(isfinite(sph.Br_nT) && isfinite(sph.Btheta_nT) &&
              isfinite(sph.Bphi_nT),
          "south pole field is finite");
    gc.theta_deg = 180.0 - 1e-6;
    CHECK_STATUS(igrf_compute_geocentric(m, 2025.0, &gc, &sph2), IGRF_OK);
    CHECK_CLOSE(sph.Br_nT, sph2.Br_nT, 1e-2, "Br continuity at south pole");
    CHECK_CLOSE(sph.Btheta_nT, sph2.Btheta_nT, 1e-2,
                "Btheta continuity at south pole");
    CHECK_CLOSE(sph.Bphi_nT, sph2.Bphi_nT, 1e-2,
                "Bphi continuity at south pole");

    /* --- geodetic poles are finite and smooth --- */
    gd.lon_deg = 0.0;
    gd.height_km = 0.0;
    gd.lat_deg = 90.0;
    CHECK_STATUS(igrf_compute_geodetic(m, 2025.0, &gd, &ned), IGRF_OK);
    CHECK(isfinite(ned.X_nT) && isfinite(ned.Y_nT) && isfinite(ned.Z_nT),
          "north pole geodetic field is finite");
    gd.lat_deg = 89.9999;
    CHECK_STATUS(igrf_compute_geodetic(m, 2025.0, &gd, &ned2), IGRF_OK);
    CHECK_CLOSE(ned.X_nT, ned2.X_nT, 0.1, "X smooth at north pole");
    CHECK_CLOSE(ned.Z_nT, ned2.Z_nT, 0.1, "Z smooth at north pole");

    gd.lat_deg = -90.0;
    CHECK_STATUS(igrf_compute_geodetic(m, 2025.0, &gd, &ned), IGRF_OK);
    CHECK(isfinite(ned.X_nT) && isfinite(ned.Y_nT) && isfinite(ned.Z_nT),
          "south pole geodetic field is finite");

    /* --- frame invariance of the total intensity F --- */
    gd.lon_deg = 121.5;
    gd.lat_deg = 25.0;
    gd.height_km = 500.0;
    CHECK_STATUS(igrf_compute_geodetic(m, 2026.5, &gd, &ned), IGRF_OK);
    CHECK_STATUS(igrf_geodetic_to_geocentric(&gd, &gc), IGRF_OK);
    CHECK_STATUS(igrf_compute_geocentric(m, 2026.5, &gc, &sph), IGRF_OK);
    CHECK_CLOSE(field_F_geodetic(&ned), field_F_spherical(&sph), 1e-9,
                "F invariance (Taiwan)");

    gd.lon_deg = -74.0;
    gd.lat_deg = 40.7;
    gd.height_km = 30000.0;
    CHECK_STATUS(igrf_compute_geodetic(m, 2010.0, &gd, &ned), IGRF_OK);
    CHECK_STATUS(igrf_geodetic_to_geocentric(&gd, &gc), IGRF_OK);
    CHECK_STATUS(igrf_compute_geocentric(m, 2010.0, &gc, &sph), IGRF_OK);
    CHECK_CLOSE(field_F_geodetic(&ned), field_F_spherical(&sph), 1e-9,
                "F invariance (30 000 km)");

    /* --- physical sanity bounds --- */
    gd.height_km = 0.0;
    CHECK_STATUS(igrf_compute_geodetic(m, 2025.0, &gd, &ned), IGRF_OK);
    CHECK(field_F_geodetic(&ned) > 20000.0 &&
              field_F_geodetic(&ned) < 70000.0,
          "surface F is in a plausible range");
    gd.height_km = 50000.0;
    CHECK_STATUS(igrf_compute_geodetic(m, 2025.0, &gd, &ned), IGRF_OK);
    CHECK(field_F_geodetic(&ned) < 5000.0,
          "F at 50000 km is much weaker than at the surface");
    gd.height_km = -100.0; /* below the ellipsoid: mathematically fine */
    CHECK_STATUS(igrf_compute_geodetic(m, 2025.0, &gd, &ned), IGRF_OK);
    CHECK(isfinite(ned.X_nT) && isfinite(ned.Y_nT) && isfinite(ned.Z_nT),
          "slightly negative height is finite");

    /* --- input validation --- */
    gd.height_km = 0.0;
    CHECK_STATUS(igrf_compute_geodetic(NULL, 2025.0, &gd, &ned),
                 IGRF_ERROR_NULL_ARGUMENT);
    CHECK_STATUS(igrf_compute_geodetic(m, 2025.0, NULL, &ned),
                 IGRF_ERROR_NULL_ARGUMENT);
    CHECK_STATUS(igrf_compute_geodetic(m, 2025.0, &gd, NULL),
                 IGRF_ERROR_NULL_ARGUMENT);
    CHECK_STATUS(igrf_compute_geocentric(NULL, 2025.0, &gc, &sph),
                 IGRF_ERROR_NULL_ARGUMENT);

    CHECK_STATUS(igrf_compute_geodetic(m, 0.0 / 0.0, &gd, &ned),
                 IGRF_ERROR_INVALID_INPUT);
    gd.lat_deg = 91.0;
    CHECK_STATUS(igrf_compute_geodetic(m, 2025.0, &gd, &ned),
                 IGRF_ERROR_INVALID_INPUT);
    gd.lat_deg = 0.0;
    gd.height_km = 1.0 / 0.0;
    CHECK_STATUS(igrf_compute_geodetic(m, 2025.0, &gd, &ned),
                 IGRF_ERROR_INVALID_INPUT);

    gc.r_km = 0.0;
    CHECK_STATUS(igrf_compute_geocentric(m, 2025.0, &gc, &sph),
                 IGRF_ERROR_INVALID_INPUT);
    gc.r_km = -6371.2;
    CHECK_STATUS(igrf_compute_geocentric(m, 2025.0, &gc, &sph),
                 IGRF_ERROR_INVALID_INPUT);
    gc.r_km = 6371.2;
    gc.theta_deg = -0.001;
    CHECK_STATUS(igrf_compute_geocentric(m, 2025.0, &gc, &sph),
                 IGRF_ERROR_INVALID_INPUT);
    gc.theta_deg = 180.001;
    CHECK_STATUS(igrf_compute_geocentric(m, 2025.0, &gc, &sph),
                 IGRF_ERROR_INVALID_INPUT);
    gc.theta_deg = 90.0;
    gc.phi_deg = 0.0 / 0.0;
    CHECK_STATUS(igrf_compute_geocentric(m, 2025.0, &gc, &sph),
                 IGRF_ERROR_INVALID_INPUT);

    igrf_model_free(m);
    return tt_report("test_igrf");
}
