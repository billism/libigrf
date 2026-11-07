/*
 * coordinates.c — coordinate and time conversions for libigrf.
 *
 * All ellipsoid parameters and conversion formulas are identical to
 * the ppigrf / IAGA-VMOD reference implementation (WGS84).
 */
#include <math.h>

#include "igrf_internal.h"

/* Days per month for common and leap years. */
static const int days_in_month[12] = {
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

static int igrf__is_leap_year(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

double igrf__normalize_longitude(double lon_deg)
{
    double lon = fmod(lon_deg, 360.0);
    if (lon < 0.0) {
        lon += 360.0;
    }
    return lon; /* in [0, 360) */
}

igrf_status_t igrf__validate_geodetic(const igrf_geodetic_t *p)
{
    if (!isfinite(p->lon_deg) || !isfinite(p->lat_deg) ||
        !isfinite(p->height_km)) {
        return IGRF_ERROR_INVALID_INPUT;
    }
    if (p->lat_deg < -90.0 || p->lat_deg > 90.0) {
        return IGRF_ERROR_INVALID_INPUT;
    }
    return IGRF_OK;
}

igrf_status_t igrf__validate_geocentric(const igrf_geocentric_t *p)
{
    if (!isfinite(p->r_km) || !isfinite(p->theta_deg) ||
        !isfinite(p->phi_deg)) {
        return IGRF_ERROR_INVALID_INPUT;
    }
    if (p->r_km <= 0.0 || p->theta_deg < 0.0 || p->theta_deg > 180.0) {
        return IGRF_ERROR_INVALID_INPUT;
    }
    return IGRF_OK;
}

static const double IGRF_PI = 3.14159265358979323846;

static double deg(double rad)
{
    return rad * (180.0 / IGRF_PI);
}

static double rad(double d)
{
    return d * (IGRF_PI / 180.0);
}

void igrf__geodetic_position_to_geocentric(double lat_deg,
                                           double height_km,
                                           double lon_deg,
                                           double *theta_deg,
                                           double *r_km,
                                           double *phi_deg)
{
    const double a = IGRF_WGS84_A_KM;
    const double b = a * sqrt(1.0 - IGRF_WGS84_E2);
    const double lat = rad(lat_deg);
    const double sin2 = sin(lat) * sin(lat);
    const double cos2 = cos(lat) * cos(lat);
    double tmp, beta, theta, r;

    /* Same formulas as geod2geoc() in the ppigrf reference. */
    tmp = height_km * sqrt(a * a * cos2 + b * b * sin2);
    beta = atan((tmp + b * b) / (tmp + a * a) * tan(lat));
    theta = IGRF_PI / 2.0 - beta;
    r = sqrt(height_km * height_km + 2.0 * tmp +
             a * a * (1.0 - (1.0 - (b / a) * (b / a) * (b / a) * (b / a)) * sin2) /
                 (1.0 - (1.0 - (b / a) * (b / a)) * sin2));

    *theta_deg = deg(theta);
    *r_km = r;
    *phi_deg = igrf__normalize_longitude(lon_deg);
}

igrf_status_t igrf_geodetic_to_geocentric(const igrf_geodetic_t *geodetic,
                                          igrf_geocentric_t *geocentric)
{
    double theta_deg, r_km, phi_deg;

    if (geodetic == NULL || geocentric == NULL) {
        return IGRF_ERROR_NULL_ARGUMENT;
    }
    if (igrf__validate_geodetic(geodetic) != IGRF_OK) {
        return IGRF_ERROR_INVALID_INPUT;
    }

    igrf__geodetic_position_to_geocentric(geodetic->lat_deg,
                                          geodetic->height_km,
                                          geodetic->lon_deg,
                                          &theta_deg, &r_km, &phi_deg);
    if (!isfinite(r_km) || r_km <= 0.0) {
        /* e.g. a height so far below the ellipsoid that the position
         * is not on any sphere of positive radius */
        return IGRF_ERROR_INVALID_INPUT;
    }

    geocentric->r_km = r_km;
    geocentric->theta_deg = theta_deg;
    geocentric->phi_deg = phi_deg;
    return IGRF_OK;
}

void igrf__spherical_to_ned(double br_nT,
                            double btheta_nT,
                            double bphi_nT,
                            double gdlat_deg,
                            double theta_deg,
                            igrf_ned_t *ned)
{
    /* psi is the angle between the geodetic and geocentric vertical
     * planes; the rotation formulas match geoc2geod() of the ppigrf
     * reference implementation. */
    const double psi = sin(rad(gdlat_deg)) * sin(rad(theta_deg)) -
                       cos(rad(gdlat_deg)) * cos(rad(theta_deg));
    const double north = -cos(psi) * btheta_nT - sin(psi) * br_nT;
    const double up    = -sin(psi) * btheta_nT + cos(psi) * br_nT;

    ned->X_nT = north;
    ned->Y_nT = bphi_nT;   /* east */
    ned->Z_nT = -up;       /* down */
}

double igrf_datetime_to_decimal_year(int year,
                                     int month,
                                     int day,
                                     int hour,
                                     int minute,
                                     double second)
{
    int leap, dim, days_before_month = 0, day_of_year, i;
    double day_fraction, days_in_year;

    if (!isfinite(second) || second < 0.0 || second >= 60.0) {
        return NAN;
    }
    if (month < 1 || month > 12 || hour < 0 || hour > 23 ||
        minute < 0 || minute > 59) {
        return NAN;
    }

    leap = igrf__is_leap_year(year);
    dim = days_in_month[month - 1];
    if (leap && month == 2) {
        dim = 29;
    }
    if (day < 1 || day > dim) {
        return NAN;
    }

    for (i = 0; i < month - 1; i++) {
        days_before_month += days_in_month[i];
    }
    if (leap && month > 2) {
        days_before_month += 1;
    }
    day_of_year = days_before_month + day; /* 1-based */

    day_fraction = ((double)(day_of_year - 1) +
                    ((double)hour * 3600.0 + (double)minute * 60.0 + second) /
                        86400.0);
    days_in_year = leap ? 366.0 : 365.0;

    return (double)year + day_fraction / days_in_year;
}

/* Days since 1970-01-01 in the proleptic Gregorian calendar
 * (Howard Hinnant's days_from_civil algorithm; exact and loop-free
 * for all reasonable years). */
static long long days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    {
        const long long era = (y >= 0 ? y : y - 399) / 400;
        const unsigned yoe = (unsigned)(y - era * 400); /* [0, 399] */
        const unsigned doy =
            (153u * (unsigned)(m + (m > 2 ? -3 : 9)) + 2u) / 5u +
            (unsigned)d - 1u; /* [0, 365] */
        const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
        return era * 146097LL + (long long)doe - 719468LL;
    }
}

/*
 * Map a decimal year onto the uniform elapsed-time axis (days in the
 * proleptic Gregorian calendar).  This is the exact inverse of
 * igrf_datetime_to_decimal_year() and matches the yearfrac-to-
 * timestamp convention of the ppigrf reference implementation:
 * start of year + fraction * (365 or 366) days.
 */
double igrf__decimal_year_to_days(double decimal_year)
{
    int year = (int)floor(decimal_year);
    double frac = decimal_year - (double)year;
    double days_in_year = igrf__is_leap_year(year) ? 366.0 : 365.0;

    return (double)days_from_civil(year, 1, 1) + frac * days_in_year;
}

igrf_status_t igrf_ned_elements(const igrf_ned_t *field,
                                double *H_nT,
                                double *F_nT,
                                double *D_deg,
                                double *I_deg)
{
    double h, f;

    if (field == NULL) {
        return IGRF_ERROR_NULL_ARGUMENT;
    }

    h = sqrt(field->X_nT * field->X_nT + field->Y_nT * field->Y_nT);
    f = sqrt(h * h + field->Z_nT * field->Z_nT);

    if (H_nT != NULL) {
        *H_nT = h;
    }
    if (F_nT != NULL) {
        *F_nT = f;
    }
    if (D_deg != NULL) {
        *D_deg = deg(atan2(field->Y_nT, field->X_nT));
    }
    if (I_deg != NULL) {
        *I_deg = deg(atan2(field->Z_nT, h));
    }
    return IGRF_OK;
}
