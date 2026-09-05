/*
 * basic.c — minimal libigrf usage example.
 *
 * Loads a model file, computes the IGRF field for a geodetic and a
 * geocentric position, prints the components, and releases the model.
 *
 * Usage: basic [model.shc]   (default: data/igrf14.shc)
 */
#include <stdio.h>
#include <stdlib.h>

#include "igrf.h"

int main(int argc, char **argv)
{
    const char *model_file = argc > 1 ? argv[1] : "data/igrf14.shc";
    igrf_model_t *model;
    igrf_geodetic_t geodetic;
    igrf_geocentric_t geocentric;
    igrf_ned_t ned;
    igrf_spherical_t spherical;
    igrf_status_t status;
    double h, f, d, i;
    double decimal_year = 2026.5;

    model = igrf_model_load(model_file);
    if (model == NULL) {
        fprintf(stderr, "error: could not load model file '%s'\n",
                model_file);
        return EXIT_FAILURE;
    }

    printf("libigrf %s, model file: %s\n", igrf_library_version(),
           model_file);

    /* --- geodetic input: longitude, latitude, height above WGS84 --- */
    geodetic.lon_deg = 121.5;   /* east-positive degrees          */
    geodetic.lat_deg = 25.0;    /* geodetic latitude              */
    geodetic.height_km = 500.0; /* km above the reference ellipsoid */

    status = igrf_compute_geodetic(model, decimal_year, &geodetic, &ned);
    if (status != IGRF_OK) {
        fprintf(stderr, "error: igrf_compute_geodetic failed (%d)\n",
                (int)status);
        igrf_model_free(model);
        return EXIT_FAILURE;
    }

    igrf_ned_elements(&ned, &h, &f, &d, &i);

    printf("\nGeodetic (2026.5, lon 121.5, lat 25.0, h 500 km):\n");
    printf("  X (north) = %10.2f nT\n", ned.X_nT);
    printf("  Y (east)  = %10.2f nT\n", ned.Y_nT);
    printf("  Z (down)  = %10.2f nT\n", ned.Z_nT);
    printf("  H         = %10.2f nT\n", h);
    printf("  F         = %10.2f nT\n", f);
    printf("  D         = %10.2f deg\n", d);
    printf("  I         = %10.2f deg\n", i);

    /* --- geocentric input: r, colatitude, longitude --- */
    geocentric.r_km = 6800.0;   /* km from the Earth's center */
    geocentric.theta_deg = 60.0; /* geocentric colatitude, [0, 180] */
    geocentric.phi_deg = 120.0; /* east-positive longitude         */

    status = igrf_compute_geocentric(model, decimal_year, &geocentric,
                                     &spherical);
    if (status != IGRF_OK) {
        fprintf(stderr, "error: igrf_compute_geocentric failed (%d)\n",
                (int)status);
        igrf_model_free(model);
        return EXIT_FAILURE;
    }

    printf("\nGeocentric (2026.5, r 6800 km, theta 60 deg, phi 120 deg):\n");
    printf("  Br    = %10.2f nT\n", spherical.Br_nT);
    printf("  Btheta= %10.2f nT\n", spherical.Btheta_nT);
    printf("  Bphi  = %10.2f nT\n", spherical.Bphi_nT);

    /* The caller owns the model and releases it. */
    igrf_model_free(model);
    return EXIT_SUCCESS;
}
