/*
 * test_field_elements.c — igrf_ned_elements() unit tests.
 */
#include <math.h>

#include "igrf.h"
#include "testutil.h"

int main(void)
{
    igrf_ned_t f;
    double h, fv, d, i;

    /* cardinal directions */
    f.X_nT = 10000.0;
    f.Y_nT = 0.0;
    f.Z_nT = 0.0;
    CHECK_STATUS(igrf_ned_elements(&f, &h, &fv, &d, &i), IGRF_OK);
    CHECK_CLOSE(h, 10000.0, 1e-12, "H east-west symmetric");
    CHECK_CLOSE(fv, 10000.0, 1e-12, "F horizontal");
    CHECK_CLOSE(d, 0.0, 1e-12, "D north");
    CHECK_CLOSE(i, 0.0, 1e-12, "I horizontal");

    f.X_nT = 0.0;
    f.Y_nT = -10000.0;
    f.Z_nT = 0.0;
    igrf_ned_elements(&f, &h, &fv, &d, &i);
    CHECK_CLOSE(d, -90.0, 1e-12, "D west");
    f.Y_nT = 10000.0;
    igrf_ned_elements(&f, &h, &fv, &d, &i);
    CHECK_CLOSE(d, 90.0, 1e-12, "D east");
    f.X_nT = 10000.0;
    f.Y_nT = -10000.0;
    igrf_ned_elements(&f, &h, &fv, &d, &i);
    CHECK_CLOSE(d, -45.0, 1e-12, "D southwest");

    f.X_nT = 0.0;
    f.Y_nT = 0.0;
    f.Z_nT = 50000.0;
    CHECK_STATUS(igrf_ned_elements(&f, &h, &fv, &d, &i), IGRF_OK);
    CHECK_CLOSE(h, 0.0, 1e-12, "H vertical");
    CHECK_CLOSE(fv, 50000.0, 1e-12, "F vertical");
    CHECK_CLOSE(i, 90.0, 1e-12, "I straight down");
    f.Z_nT = -50000.0;
    igrf_ned_elements(&f, &h, &fv, &d, &i);
    CHECK_CLOSE(i, -90.0, 1e-12, "I straight up");

    /* diagonal vector */
    f.X_nT = 1.0;
    f.Y_nT = 1.0;
    f.Z_nT = -1.0;
    CHECK_STATUS(igrf_ned_elements(&f, &h, &fv, &d, &i), IGRF_OK);
    CHECK_CLOSE(h, 1.4142135623730951, 1e-12, "H diagonal");
    CHECK_CLOSE(fv, 1.7320508075688772, 1e-12, "F diagonal");
    CHECK_CLOSE(d, 45.0, 1e-12, "D diagonal");
    CHECK_CLOSE(i, -35.264389682754654, 1e-12, "I diagonal");

    /* general vector (independent reference computation) */
    f.X_nT = 30000.123;
    f.Y_nT = -456.789;
    f.Z_nT = -47000.5;
    CHECK_STATUS(igrf_ned_elements(&f, &h, &fv, &d, &i), IGRF_OK);
    CHECK_CLOSE(h, 30003.600387381011, 1e-9, "H general");
    CHECK_CLOSE(fv, 55760.766103557522, 1e-9, "F general");
    CHECK_CLOSE(d, -0.87233174162499394, 1e-12, "D general");
    CHECK_CLOSE(i, -57.44715453977161, 1e-12, "I general");

    /* NULL output pointers select individual elements */
    f.X_nT = 3.0;
    f.Y_nT = 4.0;
    f.Z_nT = 0.0;
    h = fv = d = i = -999.0;
    CHECK_STATUS(igrf_ned_elements(&f, &h, NULL, NULL, &i), IGRF_OK);
    CHECK_CLOSE(h, 5.0, 1e-12, "H with NULL outputs");
    CHECK(fv == -999.0, "F untouched when NULL");
    CHECK(d == -999.0, "D untouched when NULL");
    CHECK_CLOSE(i, 0.0, 1e-12, "I with NULL outputs");

    /* invalid input */
    CHECK_STATUS(igrf_ned_elements(NULL, &h, &fv, &d, &i),
                 IGRF_ERROR_NULL_ARGUMENT);

    return tt_report("test_field_elements");
}
