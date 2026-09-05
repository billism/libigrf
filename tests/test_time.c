/*
 * test_time.c — igrf_datetime_to_decimal_year unit tests.
 *
 * Expected values follow the ppigrf day-count convention:
 * decimal = year + (doy - 1 + fraction of day) / days_in_year with
 * 366 days for leap years.  Reference values were computed
 * independently with the same convention (see the generator tool).
 */
#include "igrf.h"
#include "testutil.h"

int main(void)
{
    /* --- epoch start is exact --- */
    CHECK(igrf_datetime_to_decimal_year(2020, 1, 1, 0, 0, 0.0) == 2020.0,
          "2020-01-01 must be exactly 2020.0");
    CHECK(igrf_datetime_to_decimal_year(1900, 1, 1, 0, 0, 0.0) == 1900.0,
          "1900-01-01 must be exactly 1900.0");

    /* --- known values (leap and common years, day fractions) --- */
    CHECK_CLOSE(igrf_datetime_to_decimal_year(2021, 3, 28, 0, 0, 0.0),
                2021.2356164383561, 1e-12, "2021-03-28");
    CHECK_CLOSE(igrf_datetime_to_decimal_year(2020, 3, 28, 0, 0, 0.0),
                2020.2377049180327, 1e-12, "2020-03-28 (leap)");
    CHECK_CLOSE(igrf_datetime_to_decimal_year(2020, 12, 31, 23, 59, 59.0),
                2020.9999999683769, 1e-12, "2020-12-31 23:59:59");
    CHECK(igrf_datetime_to_decimal_year(2021, 7, 2, 12, 0, 0.0) == 2021.5,
          "2021-07-02 12:00 must be exactly 2021.5");
    CHECK_CLOSE(igrf_datetime_to_decimal_year(2020, 7, 1, 12, 0, 0.0),
                2020.4986338797814, 1e-12, "2020-07-01 12:00 (leap)");
    CHECK_CLOSE(igrf_datetime_to_decimal_year(2026, 1, 1, 0, 0, 0.5),
                2026.000000015855, 1e-12, "2026-01-01 +0.5 s");
    CHECK_CLOSE(igrf_datetime_to_decimal_year(2000, 2, 29, 12, 30, 15.5),
                2000.1626257178457, 1e-12, "2000-02-29 12:30:15.5");
    CHECK_CLOSE(igrf_datetime_to_decimal_year(1900, 2, 28, 0, 0, 0.0),
                1900.158904109589, 1e-12, "1900-02-28 (1900 not leap)");
    CHECK_CLOSE(igrf_datetime_to_decimal_year(2030, 6, 15, 6, 30, 0.25),
                2030.4527968115804, 1e-12, "2030-06-15 06:30:00.25");

    /* --- leap-year rules --- */
    CHECK(isfinite(igrf_datetime_to_decimal_year(2000, 2, 29, 0, 0, 0.0)),
          "2000-02-29 is valid (divisible by 400)");
    CHECK(isnan(igrf_datetime_to_decimal_year(1900, 2, 29, 0, 0, 0.0)),
          "1900-02-29 is invalid (divisible by 100 but not 400)");
    CHECK(isnan(igrf_datetime_to_decimal_year(2021, 2, 29, 0, 0, 0.0)),
          "2021-02-29 is invalid (not a leap year)");
    CHECK(isfinite(igrf_datetime_to_decimal_year(2024, 2, 29, 0, 0, 0.0)),
          "2024-02-29 is valid");

    /* --- invalid calendar inputs return NaN --- */
    CHECK(isnan(igrf_datetime_to_decimal_year(2021, 0, 1, 0, 0, 0.0)),
          "month 0 invalid");
    CHECK(isnan(igrf_datetime_to_decimal_year(2021, 13, 1, 0, 0, 0.0)),
          "month 13 invalid");
    CHECK(isnan(igrf_datetime_to_decimal_year(2021, 1, 0, 0, 0, 0.0)),
          "day 0 invalid");
    CHECK(isnan(igrf_datetime_to_decimal_year(2021, 1, 32, 0, 0, 0.0)),
          "day 32 invalid");
    CHECK(isnan(igrf_datetime_to_decimal_year(2021, 4, 31, 0, 0, 0.0)),
          "April 31 invalid");
    CHECK(isnan(igrf_datetime_to_decimal_year(2021, 1, 1, 24, 0, 0.0)),
          "hour 24 invalid");
    CHECK(isnan(igrf_datetime_to_decimal_year(2021, 1, 1, -1, 0, 0.0)),
          "hour -1 invalid");
    CHECK(isnan(igrf_datetime_to_decimal_year(2021, 1, 1, 0, 60, 0.0)),
          "minute 60 invalid");
    CHECK(isnan(igrf_datetime_to_decimal_year(2021, 1, 1, 0, 0, 60.0)),
          "second 60 invalid");
    CHECK(isnan(igrf_datetime_to_decimal_year(2021, 1, 1, 0, 0, -0.5)),
          "negative second invalid");
    CHECK(isnan(igrf_datetime_to_decimal_year(2021, 1, 1, 0, 0,
                                              0.0 / 0.0)),
          "NaN second invalid");
    CHECK(isnan(igrf_datetime_to_decimal_year(2021, 1, 1, 0, 0,
                                              1.0 / 0.0)),
          "Inf second invalid");

    return tt_report("test_time");
}
