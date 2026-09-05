/*
 * test_coordinates.c — geodetic -> geocentric conversion unit tests.
 *
 * Expected values were produced with geod2geoc() of the ppigrf
 * reference implementation (identical WGS84 ellipsoid and formulas).
 */
#include "igrf.h"
#include "testutil.h"

int main(void)
{
    igrf_geocentric_t gc;
    igrf_geodetic_t gd;

    /* equator at the prime meridian: on the semi-major axis */
    gd.lon_deg = 0.0;
    gd.lat_deg = 0.0;
    gd.height_km = 0.0;
    CHECK_STATUS(igrf_geodetic_to_geocentric(&gd, &gc), IGRF_OK);
    CHECK_CLOSE(gc.theta_deg, 90.0, 1e-9, "theta at lat 0");
    CHECK_CLOSE(gc.r_km, 6378.137, 1e-9, "r at lat 0");
    CHECK(gc.phi_deg == 0.0, "phi passthrough at lon 0");

    /* north pole: theta 0, radius of the semi-minor axis */
    gd.lat_deg = 90.0;
    CHECK_STATUS(igrf_geodetic_to_geocentric(&gd, &gc), IGRF_OK);
    CHECK_CLOSE(gc.theta_deg, 0.0, 1e-9, "theta at north pole");
    CHECK_CLOSE(gc.r_km, 6356.7523142451837, 1e-6, "r at north pole");

    /* south pole with height */
    gd.lat_deg = -90.0;
    gd.height_km = 123.0;
    CHECK_STATUS(igrf_geodetic_to_geocentric(&gd, &gc), IGRF_OK);
    CHECK_CLOSE(gc.theta_deg, 180.0, 1e-9, "theta at south pole");
    CHECK_CLOSE(gc.r_km, 6479.7523142451837, 1e-6, "r at south pole");

    /* mid-latitude values from the ppigrf reference */
    gd.lat_deg = 45.0;
    gd.height_km = 300.0;
    CHECK_STATUS(igrf_geodetic_to_geocentric(&gd, &gc), IGRF_OK);
    CHECK_CLOSE(gc.theta_deg, 45.183765254074885, 1e-9, "theta at 45 deg");
    CHECK_CLOSE(gc.r_km, 6667.4879281420808, 1e-9, "r at 45 deg");

    gd.lat_deg = -45.0;
    gd.height_km = 1234.5;
    CHECK_STATUS(igrf_geodetic_to_geocentric(&gd, &gc), IGRF_OK);
    CHECK_CLOSE(gc.theta_deg, 134.83882468382205, 1e-9,
                "theta at -45 deg");
    CHECK_CLOSE(gc.r_km, 7601.9837124813284, 1e-9, "r at -45 deg");

    gd.lat_deg = 25.0;
    gd.height_km = 500.0;
    CHECK_STATUS(igrf_geodetic_to_geocentric(&gd, &gc), IGRF_OK);
    CHECK_CLOSE(gc.theta_deg, 65.13638915942326, 1e-9, "theta at 25 deg");
    CHECK_CLOSE(gc.r_km, 6874.3423018157773, 1e-9, "r at 25 deg");

    /* longitude is passed through normalized to [0, 360) */
    gd.lat_deg = 10.0;
    gd.height_km = 0.0;
    gd.lon_deg = 121.5;
    CHECK_STATUS(igrf_geodetic_to_geocentric(&gd, &gc), IGRF_OK);
    CHECK(gc.phi_deg == 121.5, "phi passthrough");
    gd.lon_deg = 360.0;
    CHECK_STATUS(igrf_geodetic_to_geocentric(&gd, &gc), IGRF_OK);
    CHECK(gc.phi_deg == 0.0, "lon 360 normalizes to 0");
    gd.lon_deg = -180.0;
    CHECK_STATUS(igrf_geodetic_to_geocentric(&gd, &gc), IGRF_OK);
    CHECK(gc.phi_deg == 180.0, "lon -180 normalizes to 180");
    gd.lon_deg = -90.0;
    CHECK_STATUS(igrf_geodetic_to_geocentric(&gd, &gc), IGRF_OK);
    CHECK(gc.phi_deg == 270.0, "lon -90 normalizes to 270");

    /* error handling */
    CHECK_STATUS(igrf_geodetic_to_geocentric(NULL, &gc),
                 IGRF_ERROR_NULL_ARGUMENT);
    CHECK_STATUS(igrf_geodetic_to_geocentric(&gd, NULL),
                 IGRF_ERROR_NULL_ARGUMENT);
    gd.lon_deg = 0.0;
    gd.lat_deg = 90.5;
    CHECK_STATUS(igrf_geodetic_to_geocentric(&gd, &gc),
                 IGRF_ERROR_INVALID_INPUT);
    gd.lat_deg = -90.5;
    CHECK_STATUS(igrf_geodetic_to_geocentric(&gd, &gc),
                 IGRF_ERROR_INVALID_INPUT);
    gd.lat_deg = 0.0;
    gd.lon_deg = 0.0 / 0.0;
    CHECK_STATUS(igrf_geodetic_to_geocentric(&gd, &gc),
                 IGRF_ERROR_INVALID_INPUT);
    gd.lon_deg = 0.0;
    gd.height_km = 1.0 / 0.0;
    CHECK_STATUS(igrf_geodetic_to_geocentric(&gd, &gc),
                 IGRF_ERROR_INVALID_INPUT);

    return tt_report("test_coordinates");
}
