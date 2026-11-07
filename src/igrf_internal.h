/*
 * igrf_internal.h — internal declarations shared between libigrf
 * translation units.  Not installed, not part of the public API.
 */
#ifndef IGRF_INTERNAL_H
#define IGRF_INTERNAL_H

#include "igrf.h"

/*
 * IGRF reference radius [km] — the radius the Gauss coefficients are
 * referred to, identical to the ppigrf reference implementation.
 */
#define IGRF_REF_RADIUS_KM 6371.2

/* WGS84 ellipsoid parameters, identical to the ppigrf reference. */
#define IGRF_WGS84_A_KM 6378.137              /* semi-major axis [km] */
#define IGRF_WGS84_E2   0.00669437999014      /* first eccentricity squared */

/* Defensive limits for .shc content; anything beyond these is
 * rejected as IGRF_ERROR_INVALID_MODEL.  IGRF-14 uses degree 13 and
 * 27 epochs, so the limits leave generous headroom. */
#define IGRF_MAX_MODEL_DEGREE 100
#define IGRF_MAX_MODEL_EPOCHS 10000

/* Number of doubles in one (n_max+1)x(n_max+1) coefficient matrix. */
#define IGRF_NM(nmax) (((size_t)(nmax) + 1) * ((size_t)(nmax) + 1))

/* Index into an epoch's coefficient matrix (or a Legendre matrix):
 * row n = degree, column m = order. */
#define IGRF_IDX(n, m, nmax) ((size_t)(n) * ((size_t)(nmax) + 1) + (size_t)(m))

/*
 * Opaque model type.  Coefficients are stored per epoch as Schmidt
 * quasi-normalized-ready raw Gauss coefficient matrices:
 * g[e][n][m] is the cosine (m >= 0) and h[e][n][m] the sine
 * coefficient of degree n, order m at epoch epochs[e].  Entries with
 * m > n or n < n_min are zero.
 */
struct igrf_model {
    int      n_max;     /* maximum spherical harmonic degree            */
    int      n_epochs;  /* number of epochs, >= 1                       */
    double  *epochs;    /* [n_epochs] decimal years, strictly ascending */
    double  *g;         /* [n_epochs * IGRF_NM(n_max)]                  */
    double  *h;         /* [n_epochs * IGRF_NM(n_max)]                  */
};

/* ---------------------------------------------------------------- */
/* Internal helpers (implemented in igrf.c)                          */
/* ---------------------------------------------------------------- */

/*
 * Interpolate the model's Gauss coefficients at decimal_year into
 * caller-provided buffers g/h of IGRF_NM(model->n_max) doubles each.
 * Linear interpolation between the two neighbouring epochs is
 * performed on the uniform elapsed-time axis: decimal years are
 * mapped to days first, matching the ppigrf reference
 * implementation (see docs/DECIMAL_YEAR_VS_TIMESTAMP.md).
 */
igrf_status_t igrf__prepare_coefficients(const igrf_model_t *model,
                                         double decimal_year,
                                         double *g,
                                         double *h);

/*
 * Map a decimal year onto the uniform elapsed-time axis (proleptic
 * Gregorian days, 365/366 day-count).  Exact inverse of
 * igrf_datetime_to_decimal_year(); implemented in coordinates.c.
 */
double igrf__decimal_year_to_days(double decimal_year);

/*
 * Evaluate the spherical harmonic field at one geocentric position
 * using pre-computed coefficient matrices g/h (layout
 * IGRF_NM(n_max)) and Legendre workspace arrays P/dP of the same
 * size.  phi_deg may be any finite longitude.
 */
void igrf__eval_field(int n_max,
                      const double *g,
                      const double *h,
                      double *P,
                      double *dP,
                      const igrf_geocentric_t *position,
                      igrf_spherical_t *field);

/* ---------------------------------------------------------------- */
/* Internal helpers (implemented in coordinates.c)                   */
/* ---------------------------------------------------------------- */

/* Normalize a longitude to [0, 360). */
double igrf__normalize_longitude(double lon_deg);

/* Validate a geodetic position (finite values, lat in [-90, 90]). */
igrf_status_t igrf__validate_geodetic(const igrf_geodetic_t *p);

/* Validate a geocentric position (finite values, r > 0, theta in
 * [0, 180]). */
igrf_status_t igrf__validate_geocentric(const igrf_geocentric_t *p);

/*
 * Geodetic -> geocentric position conversion on WGS84 (position only,
 * field components untouched).  Same formulas as the ppigrf reference
 * implementation.  lon_deg is passed through normalized.
 */
void igrf__geodetic_position_to_geocentric(double lat_deg,
                                           double height_km,
                                           double lon_deg,
                                           double *theta_deg,
                                           double *r_km,
                                           double *phi_deg);

/*
 * Rotate geocentric spherical field components into the local
 * geodetic North-East-Down frame at (gdlat_deg, theta_deg).
 * Br/Btheta/Bphi -> X (north), Y (east), Z (down).
 */
void igrf__spherical_to_ned(double br_nT,
                            double btheta_nT,
                            double bphi_nT,
                            double gdlat_deg,
                            double theta_deg,
                            igrf_ned_t *ned);

#endif /* IGRF_INTERNAL_H */
