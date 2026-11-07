/*
 * igrf.h — Public API of libigrf
 *
 * A small, portable, dependency-free C library for computing the
 * International Geomagnetic Reference Field (IGRF) from spherical
 * harmonic coefficient (.shc) model files.
 *
 * Conventions
 * -----------
 *  - Geodetic input uses WGS84 (longitude east-positive in degrees,
 *    geodetic latitude in degrees, height above the reference
 *    ellipsoid in km).
 *  - Geocentric input uses Earth-centered spherical coordinates
 *    (r in km from the Earth's center, theta = geocentric colatitude
 *    in degrees, phi = east-positive longitude in degrees):
 *
 *        theta = 0    -> North pole
 *        theta = 90   -> Equator
 *        theta = 180  -> South pole
 *
 *  - The spherical harmonic expansion is always evaluated in the
 *    geocentric spherical coordinate system, exactly as IGRF is
 *    defined.  Geodetic input is converted to geocentric coordinates
 *    internally, and the geocentric field components are transformed
 *    back to the local geodetic North-East-Down frame before being
 *    returned as X, Y, Z.
 *  - Field components are in nanotesla (nT).  Btheta points in the
 *    direction of increasing theta (i.e. from north towards south),
 *    Bphi points east.  For geodetic input, X = north, Y = east,
 *    Z = down.
 *  - Longitude inputs accept any finite value; they are normalized
 *    internally, so 360, -180 and 0 all denote the Greenwich
 *    meridian without corner-case behaviour.
 *  - Time is a decimal year (e.g. 2026.0, 2026.5, 2027.25).  Use
 *    igrf_datetime_to_decimal_year() to convert calendar date/time.
 *    The supported year range is [first_epoch, last_epoch] of the
 *    loaded model file; requests outside that range return
 *    IGRF_ERROR_YEAR_OUT_OF_RANGE (no silent extrapolation).
 *
 * Memory ownership
 * ----------------
 *  igrf_model_load() allocates the model; release it with
 *  igrf_model_free().  Batch functions write into caller-provided
 *  output arrays; the library never allocates memory the caller must
 *  track besides the model itself.
 *
 * Error handling
 * --------------
 *  Functions return igrf_status_t (or NULL for igrf_model_load on
 *  failure).  The library never prints, exits or aborts; callers are
 *  expected to check status codes.  Input values that are NaN or Inf
 *  are rejected with IGRF_ERROR_INVALID_INPUT.
 *
 * The API is plain C (C99) and C++-linkable via extern "C".
 */
#ifndef IGRF_H
#define IGRF_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Symbol visibility for shared-library builds.
 * When libigrf is built as a DLL by CMake, the toolchain defines
 * igrf_EXPORTS while compiling the library and the public functions
 * are exported with dllexport.  On the consumer side (and for static
 * and non-Windows builds) the macro is empty: MSVC resolves the
 * functions through the import-library thunks, and MinGW/Unix need
 * no decoration at all.
 */
#ifndef IGRF_API
#  if defined(_WIN32) && defined(igrf_EXPORTS)
#    define IGRF_API __declspec(dllexport)
#  else
#    define IGRF_API
#  endif
#endif

/* ---------------------------------------------------------------- */
/* Status codes                                                      */
/* ---------------------------------------------------------------- */

typedef enum {
    IGRF_OK = 0,                /* success                                 */
    IGRF_ERROR_NULL_ARGUMENT,   /* a required pointer argument was NULL    */
    IGRF_ERROR_INVALID_INPUT,   /* NaN/Inf or out-of-domain coordinate,    */
                                /* invalid count, or invalid date/time     */
    IGRF_ERROR_FILE,            /* model file could not be opened or read  */
    IGRF_ERROR_INVALID_MODEL,   /* file was readable but is not a valid    */
                                /* .shc model                              */
    IGRF_ERROR_YEAR_OUT_OF_RANGE /* decimal year outside the model's       */
                                 /* epoch range                            */
} igrf_status_t;

/* ---------------------------------------------------------------- */
/* Types                                                             */
/* ---------------------------------------------------------------- */

/* Opaque model handle loaded from a .shc file. */
typedef struct igrf_model igrf_model_t;

/* Geodetic position (WGS84 ellipsoid). */
typedef struct {
    double lon_deg;    /* east-positive longitude [degree]          */
    double lat_deg;    /* geodetic latitude, [-90, 90] [degree]     */
    double height_km;  /* height above the reference ellipsoid [km] */
} igrf_geodetic_t;

/* Geocentric spherical position (Earth-centered). */
typedef struct {
    double r_km;       /* distance from Earth's center, > 0 [km]    */
    double theta_deg;  /* geocentric colatitude, [0, 180] [degree]  */
    double phi_deg;    /* east-positive longitude [degree]          */
} igrf_geocentric_t;

/* Magnetic field in the local geodetic North-East-Down frame [nT]. */
typedef struct {
    double X_nT;       /* north component                           */
    double Y_nT;       /* east component                            */
    double Z_nT;       /* down component                            */
} igrf_ned_t;

/* Magnetic field in geocentric spherical components [nT].
 * Btheta points towards increasing theta (southward on a sphere),
 * Bphi points east. */
typedef struct {
    double Br_nT;      /* radial component                          */
    double Btheta_nT;  /* colatitude (southward) component          */
    double Bphi_nT;    /* eastward component                        */
} igrf_spherical_t;

/* Geodetic position tagged with its own decimal year, for
 * varying-time batch computation. */
typedef struct {
    double decimal_year;
    double lon_deg;
    double lat_deg;
    double height_km;
} igrf_geodetic_time_point_t;

/* Geocentric position tagged with its own decimal year, for
 * varying-time batch computation. */
typedef struct {
    double decimal_year;
    double r_km;
    double theta_deg;
    double phi_deg;
} igrf_geocentric_time_point_t;

/* ---------------------------------------------------------------- */
/* Model management                                                  */
/* ---------------------------------------------------------------- */

/*
 * Load an IGRF model from a .shc spherical harmonic coefficient file.
 * The file format is the standard SHC text format used by IGRF:
 * '#' lines are comments, followed by a header line, a line of epoch
 * decimal years, and one "n m c1 c2 ..." line per Gauss coefficient.
 * m >= 0 lines are the g (cosine) coefficients, m < 0 lines the
 * h (sine) coefficients.
 *
 * The model must be released with igrf_model_free().
 * Returns NULL on failure; use the returned status-free NULL to
 * distinguish file problems (unopenable file) from content problems:
 * both simply yield NULL.  filename must not be NULL.
 */
IGRF_API igrf_model_t *igrf_model_load(const char *filename);

/* Release a model previously returned by igrf_model_load().
 * Passing NULL is a no-op. */
IGRF_API void igrf_model_free(igrf_model_t *model);

/* ---------------------------------------------------------------- */
/* Single-point computation                                          */
/* ---------------------------------------------------------------- */

/*
 * Compute IGRF for a geodetic position.  Internally the position is
 * converted to geocentric spherical coordinates, the spherical
 * harmonic expansion is evaluated there, and the resulting field is
 * transformed to the local geodetic NED frame at the same position.
 *
 * decimal_year must lie within the model's epoch range (the first
 * and last decimal years listed in the .shc file).  Out-of-range
 * years are rejected with IGRF_ERROR_YEAR_OUT_OF_RANGE instead of
 * being extrapolated.
 */
IGRF_API igrf_status_t igrf_compute_geodetic(
    const igrf_model_t   *model,
    double                decimal_year,
    const igrf_geodetic_t *position,
    igrf_ned_t            *field);

/*
 * Compute IGRF for a geocentric spherical position.  Returns the
 * field in geocentric spherical components (Br, Btheta, Bphi).
 * theta must be in [0, 180] degrees; the field is finite at both
 * poles (Bphi = 0 there, which is the analytic limit).
 */
IGRF_API igrf_status_t igrf_compute_geocentric(
    const igrf_model_t     *model,
    double                  decimal_year,
    const igrf_geocentric_t *position,
    igrf_spherical_t        *field);

/* ---------------------------------------------------------------- */
/* Fixed-time batch computation                                      */
/* ---------------------------------------------------------------- */

/*
 * Compute IGRF for `count` geodetic positions at one common decimal
 * year.  The time-dependent coefficient interpolation is performed
 * exactly once for the whole batch; positions[i] is written to
 * fields[i].  positions and fields must not be NULL and count must
 * be > 0.  fields may alias positions only if the element types have
 * the same size and layout (they do not; do not alias).
 */
IGRF_API igrf_status_t igrf_compute_geodetic_batch(
    const igrf_model_t    *model,
    double                 decimal_year,
    const igrf_geodetic_t *positions,
    size_t                 count,
    igrf_ned_t            *fields);

/*
 * Compute IGRF for `count` geocentric positions at one common decimal
 * year.  Same preparation-once semantics as the geodetic batch.
 */
IGRF_API igrf_status_t igrf_compute_geocentric_batch(
    const igrf_model_t      *model,
    double                   decimal_year,
    const igrf_geocentric_t *positions,
    size_t                   count,
    igrf_spherical_t        *fields);

/* ---------------------------------------------------------------- */
/* Varying-time batch computation                                    */
/* ---------------------------------------------------------------- */

/*
 * Compute IGRF for `count` geodetic positions, each with its own
 * decimal year.  points[i] is written to fields[i].
 */
IGRF_API igrf_status_t igrf_compute_geodetic_batch_varying_time(
    const igrf_model_t             *model,
    const igrf_geodetic_time_point_t *points,
    size_t                          count,
    igrf_ned_t                     *fields);

/*
 * Compute IGRF for `count` geocentric positions, each with its own
 * decimal year.  points[i] is written to fields[i].
 */
IGRF_API igrf_status_t igrf_compute_geocentric_batch_varying_time(
    const igrf_model_t                *model,
    const igrf_geocentric_time_point_t *points,
    size_t                             count,
    igrf_spherical_t                  *fields);

/* ---------------------------------------------------------------- */
/* Coordinate and time conversion helpers                            */
/* ---------------------------------------------------------------- */

/*
 * Convert a geodetic position (WGS84) to geocentric spherical
 * coordinates.  The longitude is copied through (normalized like all
 * longitude inputs).  Uses the same ellipsoid and formulas as the
 * IGRF/ppigrf reference implementation.
 */
IGRF_API igrf_status_t igrf_geodetic_to_geocentric(
    const igrf_geodetic_t  *geodetic,
    igrf_geocentric_t      *geocentric);

/*
 * Convert a calendar date/time to a decimal year.
 *
 * decimal_year = year + (day-of-year - 1 + fraction of day) / days_in_year
 *
 * where days_in_year is 366 for leap years and 365 otherwise (the
 * same convention as the ppigrf reference implementation).  Example:
 * 2021-03-28 00:00:00 -> 2021.2356...
 *
 * Ranges: month [1,12], day [1,last day of month], hour [0,23],
 * minute [0,59], second [0,60).  Invalid input returns NaN.
 */
IGRF_API double igrf_datetime_to_decimal_year(
    int    year,
    int    month,
    int    day,
    int    hour,
    int    minute,
    double second);

/* ---------------------------------------------------------------- */
/* Derived field elements                                            */
/* ---------------------------------------------------------------- */

/*
 * Compute derived elements from a geodetic NED field:
 *
 *   H = sqrt(X^2 + Y^2)          horizontal intensity [nT]
 *   F = sqrt(X^2 + Y^2 + Z^2)    total intensity [nT]
 *   D = atan2(Y, X)              declination [degree]
 *   I = atan2(Z, H)              inclination [degree]
 *
 * field must not be NULL; any of the output pointers may be NULL to
 * skip that element.
 */
IGRF_API igrf_status_t igrf_ned_elements(
    const igrf_ned_t *field,
    double           *H_nT,
    double           *F_nT,
    double           *D_deg,
    double           *I_deg);

/* ---------------------------------------------------------------- */
/* Introspection                                                     */
/* ---------------------------------------------------------------- */

/*
 * Version of the library itself (not the IGRF model generation),
 * e.g. "1.0.0".  The returned string is static and owned by the
 * library.
 */
IGRF_API const char *igrf_library_version(void);

#ifdef __cplusplus
}
#endif

#endif /* IGRF_H */
