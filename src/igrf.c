/*
 * igrf.c — core IGRF spherical harmonic computation for libigrf.
 *
 * The mathematical conventions (Schmidt semi-normalized associated
 * Legendre functions via the Wertz recursion, radial scaling with the
 * reference radius 6371.2 km, and the sign conventions of Br, Btheta,
 * Bphi) follow the ppigrf reference implementation exactly.
 *
 * The spherical harmonic expansion is evaluated in geocentric
 * spherical coordinates, as IGRF is defined.  Geodetic positions are
 * converted to geocentric (WGS84) and the resulting field rotated
 * into the local geodetic NED frame.
 *
 * Pole handling: at theta = 0 the sin(theta) division of the Bphi
 * summation is 0/0.  The analytic limit of each m >= 1 term exists
 * (P(n,m) ~ sin(theta)^m), and the limit of the m = 1 term is
 * dP(n,1)/dtheta evaluated at the pole, taken along the meridian of
 * the requested longitude.  The library returns that smooth
 * along-meridian limit, so field values stay finite and continuous
 * when approaching the poles; no NaN/Inf is ever produced.
 */
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "igrf_internal.h"

#define IGRF_VERSION "1.0.0"

static const double IGRF_PI = 3.14159265358979323846;

static double igrf__deg_to_rad(double deg)
{
    return deg * (IGRF_PI / 180.0);
}

/* ---------------------------------------------------------------- */
/* Workspace                                                         */
/* ---------------------------------------------------------------- */

typedef struct {
    double *g;   /* interpolated coefficients, IGRF_NM(n_max)   */
    double *h;
    double *P;   /* Schmidt quasi-normalized Legendre            */
    double *dP;  /* dP/dtheta                                    */
} igrf__work;

static int igrf__work_alloc(igrf__work *w, int n_max)
{
    size_t nm = IGRF_NM(n_max);

    w->g = (double *)calloc(nm, sizeof(double));
    w->h = (double *)calloc(nm, sizeof(double));
    w->P = (double *)calloc(nm, sizeof(double));
    w->dP = (double *)calloc(nm, sizeof(double));
    if (w->g == NULL || w->h == NULL || w->P == NULL || w->dP == NULL) {
        free(w->g);
        free(w->h);
        free(w->P);
        free(w->dP);
        w->g = w->h = w->P = w->dP = NULL;
        return -1;
    }
    return 0;
}

static void igrf__work_free(igrf__work *w)
{
    free(w->g);
    free(w->h);
    free(w->P);
    free(w->dP);
    w->g = w->h = w->P = w->dP = NULL;
}

/* ---------------------------------------------------------------- */
/* Time interpolation of the Gauss coefficients                      */
/* ---------------------------------------------------------------- */

igrf_status_t igrf__prepare_coefficients(const igrf_model_t *model,
                                         double decimal_year,
                                         double *g,
                                         double *h)
{
    const double *epochs = model->epochs;
    const size_t nm = IGRF_NM(model->n_max);
    size_t k;
    int i, n_epochs = model->n_epochs;

    if (!isfinite(decimal_year)) {
        return IGRF_ERROR_INVALID_INPUT;
    }
    if (decimal_year < epochs[0] || decimal_year > epochs[n_epochs - 1]) {
        return IGRF_ERROR_YEAR_OUT_OF_RANGE;
    }

    /* exactly on an epoch: use that epoch's coefficients */
    for (i = 0; i < n_epochs; i++) {
        if (decimal_year == epochs[i]) {
            memcpy(g, model->g + (size_t)i * nm, nm * sizeof(double));
            memcpy(h, model->h + (size_t)i * nm, nm * sizeof(double));
            return IGRF_OK;
        }
    }

    /* strictly between two epochs: linear interpolation on the
     * uniform elapsed-time axis, exactly like the ppigrf reference
     * implementation (see IMPLEMENTATION_NOTES.md section 1 for the
     * convention analysis behind this decision) */
    for (i = 0; i < n_epochs - 1; i++) {
        if (decimal_year < epochs[i + 1]) {
            const double *g1 = model->g + (size_t)i * nm;
            const double *g2 = model->g + (size_t)(i + 1) * nm;
            const double *h1 = model->h + (size_t)i * nm;
            const double *h2 = model->h + (size_t)(i + 1) * nm;
            const double t = igrf__decimal_year_to_days(decimal_year);
            const double t1 = igrf__decimal_year_to_days(epochs[i]);
            const double t2 = igrf__decimal_year_to_days(epochs[i + 1]);
            const double f = (t - t1) / (t2 - t1);

            for (k = 0; k < nm; k++) {
                g[k] = g1[k] + f * (g2[k] - g1[k]);
                h[k] = h1[k] + f * (h2[k] - h1[k]);
            }
            return IGRF_OK;
        }
    }

    /* unreachable for a consistent model, kept as a safety net */
    return IGRF_ERROR_YEAR_OUT_OF_RANGE;
}

/* ---------------------------------------------------------------- */
/* Schmidt quasi-normalized associated Legendre functions            */
/* ---------------------------------------------------------------- */

/*
 * Fill P[n][m] = P~(n, m, theta) and dP[n][m] = dP~/dtheta with the
 * Schmidt semi-normalized associated Legendre functions and their
 * theta derivatives, using the recursion from "Spacecraft Attitude
 * Determination and Control" (Wertz), as in the ppigrf reference.
 * P and dP are IGRF_NM(n_max) buffers and are fully reinitialized.
 */
static void igrf__legendre(int n_max, double theta_deg, double *P, double *dP)
{
    const double theta_rad = igrf__deg_to_rad(theta_deg);
    const double sinth = sin(theta_rad);
    const double costh = cos(theta_rad);
    const size_t nm = IGRF_NM(n_max);
    double s_row_prev0 = 1.0; /* S[n-1][0] during the normalization pass */
    double s_cur;
    int n, m;
    size_t k;

    for (k = 0; k < nm; k++) {
        P[k] = 0.0;
        dP[k] = 0.0;
    }
    P[IGRF_IDX(0, 0, n_max)] = 1.0;
    /* dP[0][0] = 0 */

    for (n = 1; n <= n_max; n++) {
        for (m = 0; m <= n; m++) {
            size_t idx = IGRF_IDX(n, m, n_max);
            size_t prev = IGRF_IDX(n - 1, m, n_max);
            size_t prev2 = IGRF_IDX(n - 2, m, n_max);

            if (n == m) {
                size_t diag = IGRF_IDX(n - 1, m - 1, n_max);
                P[idx] = sinth * P[diag];
                dP[idx] = sinth * dP[diag] + costh * P[diag];
            } else if (n == 1) {
                /* m == 0: Knm = 0 */
                P[idx] = costh * P[prev];
                dP[idx] = costh * dP[prev] - sinth * P[prev];
            } else {
                double knm = ((double)((n - 1) * (n - 1)) - (double)(m * m)) /
                             ((double)(2 * n - 1) * (double)(2 * n - 3));
                P[idx] = costh * P[prev] - knm * P[prev2];
                dP[idx] = costh * dP[prev] - sinth * P[prev] -
                          knm * dP[prev2];
            }
        }
    }

    /* Apply the Schmidt normalization factors S[n][m], recomputed on
     * the fly in the same order as the recursion above. */
    s_cur = 1.0;
    for (n = 1; n <= n_max; n++) {
        double s_row0 = 1.0;

        for (m = 0; m <= n; m++) {
            size_t idx = IGRF_IDX(n, m, n_max);

            if (m == 0) {
                s_cur = s_row_prev0 * (2.0 * n - 1.0) / (double)n;
                s_row0 = s_cur;
            } else {
                s_cur *= sqrt((double)(n - m + 1) * (m == 1 ? 2.0 : 1.0) /
                              (double)(n + m));
            }
            P[idx] *= s_cur;
            dP[idx] *= s_cur;
        }
        s_row_prev0 = s_row0; /* S[n][0] for the next row */
    }
}

/* ---------------------------------------------------------------- */
/* Spherical harmonic evaluation at one position                     */
/* ---------------------------------------------------------------- */

void igrf__eval_field(int n_max,
                      const double *g,
                      const double *h,
                      double *P,
                      double *dP,
                      const igrf_geocentric_t *position,
                      igrf_spherical_t *field)
{
    const double phi_rad = igrf__deg_to_rad(
        igrf__normalize_longitude(position->phi_deg));
    const double sinth = sin(igrf__deg_to_rad(position->theta_deg));
    const double ratio = IGRF_REF_RADIUS_KM / position->r_km;
    double br = 0.0, btheta = 0.0, bphi = 0.0;
    double radial_pow; /* (RE / r)^(n + 2) */
    int n, m;

    igrf__legendre(n_max, position->theta_deg, P, dP);

    radial_pow = ratio * ratio * ratio; /* n = 1: (RE/r)^3 */
    for (n = 1; n <= n_max; n++) {
        const double n1 = (double)(n + 1);

        for (m = 0; m <= n; m++) {
            const size_t idx = IGRF_IDX(n, m, n_max);
            const double gc = g[idx];
            const double hc = h[idx];
            const double pv = P[idx];
            const double dpv = dP[idx];

            if (m == 0) {
                br += radial_pow * n1 * pv * gc;
                btheta -= radial_pow * dpv * gc;
            } else {
                const double arg = (double)m * phi_rad;
                const double cm = cos(arg);
                const double sm = sin(arg);

                br += radial_pow * n1 * pv * (gc * cm + hc * sm);
                btheta -= radial_pow * dpv * (gc * cm + hc * sm);
                bphi += radial_pow * (double)m * pv * (gc * sm - hc * cm);
            }
        }
        radial_pow *= ratio;
    }

    if (sinth == 0.0) {
        /* Exact pole: bphi currently accumulates only m >= 1 terms
         * whose P values vanish at sin(theta) = 0.  Replace them by
         * their smooth limit along the requested meridian, where
         * P(n, 1, theta)/sin(theta) -> dP(n, 1)/dtheta at the pole. */
        double bphi_pole = 0.0;

        radial_pow = ratio * ratio * ratio;
        for (n = 1; n <= n_max; n++) {
            const size_t idx1 = IGRF_IDX(n, 1, n_max);

            bphi_pole += radial_pow * dP[idx1] *
                         (g[idx1] * sin(phi_rad) - h[idx1] * cos(phi_rad));
            radial_pow *= ratio;
        }
        bphi = bphi_pole;
    } else {
        bphi /= sinth;
    }

    field->Br_nT = br;
    field->Btheta_nT = btheta;
    field->Bphi_nT = bphi;
}

/* ---------------------------------------------------------------- */
/* Internal single-point drivers                                     */
/* ---------------------------------------------------------------- */

static igrf_status_t igrf__compute_geocentric_with_work(
    const igrf_model_t *model,
    const double *g,
    const double *h,
    double *P,
    double *dP,
    const igrf_geocentric_t *position,
    igrf_spherical_t *field)
{
    igrf__eval_field(model->n_max, g, h, P, dP, position, field);
    return IGRF_OK;
}

static igrf_status_t igrf__compute_geodetic_with_work(
    const igrf_model_t *model,
    const double *g,
    const double *h,
    double *P,
    double *dP,
    const igrf_geodetic_t *position,
    igrf_ned_t *field)
{
    igrf_geocentric_t gc;
    igrf_spherical_t spherical;

    igrf__geodetic_position_to_geocentric(position->lat_deg,
                                          position->height_km,
                                          position->lon_deg,
                                          &gc.theta_deg, &gc.r_km,
                                          &gc.phi_deg);
    igrf__eval_field(model->n_max, g, h, P, dP, &gc, &spherical);
    igrf__spherical_to_ned(spherical.Br_nT, spherical.Btheta_nT,
                           spherical.Bphi_nT, position->lat_deg,
                           gc.theta_deg, field);
    return IGRF_OK;
}

/* ---------------------------------------------------------------- */
/* Public API: single-point computation                              */
/* ---------------------------------------------------------------- */

igrf_status_t igrf_compute_geocentric(const igrf_model_t *model,
                                      double decimal_year,
                                      const igrf_geocentric_t *position,
                                      igrf_spherical_t *field)
{
    igrf__work work;
    igrf_status_t status;

    if (model == NULL || position == NULL || field == NULL) {
        return IGRF_ERROR_NULL_ARGUMENT;
    }
    status = igrf__validate_geocentric(position);
    if (status != IGRF_OK) {
        return status;
    }
    if (igrf__work_alloc(&work, model->n_max) != 0) {
        return IGRF_ERROR_INVALID_MODEL; /* out of memory */
    }

    status = igrf__prepare_coefficients(model, decimal_year, work.g, work.h);
    if (status == IGRF_OK) {
        status = igrf__compute_geocentric_with_work(model, work.g, work.h,
                                                    work.P, work.dP,
                                                    position, field);
    }

    igrf__work_free(&work);
    return status;
}

igrf_status_t igrf_compute_geodetic(const igrf_model_t *model,
                                    double decimal_year,
                                    const igrf_geodetic_t *position,
                                    igrf_ned_t *field)
{
    igrf__work work;
    igrf_status_t status;

    if (model == NULL || position == NULL || field == NULL) {
        return IGRF_ERROR_NULL_ARGUMENT;
    }
    status = igrf__validate_geodetic(position);
    if (status != IGRF_OK) {
        return status;
    }
    if (igrf__work_alloc(&work, model->n_max) != 0) {
        return IGRF_ERROR_INVALID_MODEL; /* out of memory */
    }

    status = igrf__prepare_coefficients(model, decimal_year, work.g, work.h);
    if (status == IGRF_OK) {
        status = igrf__compute_geodetic_with_work(model, work.g, work.h,
                                                  work.P, work.dP,
                                                  position, field);
    }

    igrf__work_free(&work);
    return status;
}

/* ---------------------------------------------------------------- */
/* Public API: fixed-time batch computation                          */
/* ---------------------------------------------------------------- */

igrf_status_t igrf_compute_geocentric_batch(const igrf_model_t *model,
                                            double decimal_year,
                                            const igrf_geocentric_t *positions,
                                            size_t count,
                                            igrf_spherical_t *fields)
{
    igrf__work work;
    igrf_status_t status;
    size_t i;

    if (model == NULL || positions == NULL || fields == NULL) {
        return IGRF_ERROR_NULL_ARGUMENT;
    }
    if (count == 0) {
        return IGRF_ERROR_INVALID_INPUT;
    }
    for (i = 0; i < count; i++) {
        status = igrf__validate_geocentric(&positions[i]);
        if (status != IGRF_OK) {
            return status;
        }
    }
    if (igrf__work_alloc(&work, model->n_max) != 0) {
        return IGRF_ERROR_INVALID_MODEL; /* out of memory */
    }

    /* time-dependent preparation happens exactly once for the batch */
    status = igrf__prepare_coefficients(model, decimal_year, work.g, work.h);
    for (i = 0; status == IGRF_OK && i < count; i++) {
        igrf__eval_field(model->n_max, work.g, work.h, work.P, work.dP,
                         &positions[i], &fields[i]);
    }

    igrf__work_free(&work);
    return status;
}

igrf_status_t igrf_compute_geodetic_batch(const igrf_model_t *model,
                                          double decimal_year,
                                          const igrf_geodetic_t *positions,
                                          size_t count,
                                          igrf_ned_t *fields)
{
    igrf__work work;
    igrf_status_t status;
    size_t i;

    if (model == NULL || positions == NULL || fields == NULL) {
        return IGRF_ERROR_NULL_ARGUMENT;
    }
    if (count == 0) {
        return IGRF_ERROR_INVALID_INPUT;
    }
    for (i = 0; i < count; i++) {
        status = igrf__validate_geodetic(&positions[i]);
        if (status != IGRF_OK) {
            return status;
        }
    }
    if (igrf__work_alloc(&work, model->n_max) != 0) {
        return IGRF_ERROR_INVALID_MODEL; /* out of memory */
    }

    status = igrf__prepare_coefficients(model, decimal_year, work.g, work.h);
    for (i = 0; status == IGRF_OK && i < count; i++) {
        igrf_geocentric_t gc;
        igrf_spherical_t spherical;

        igrf__geodetic_position_to_geocentric(positions[i].lat_deg,
                                              positions[i].height_km,
                                              positions[i].lon_deg,
                                              &gc.theta_deg, &gc.r_km,
                                              &gc.phi_deg);
        igrf__eval_field(model->n_max, work.g, work.h, work.P, work.dP,
                         &gc, &spherical);
        igrf__spherical_to_ned(spherical.Br_nT, spherical.Btheta_nT,
                               spherical.Bphi_nT, positions[i].lat_deg,
                               gc.theta_deg, &fields[i]);
    }

    igrf__work_free(&work);
    return status;
}

/* ---------------------------------------------------------------- */
/* Public API: varying-time batch computation                        */
/* ---------------------------------------------------------------- */

igrf_status_t igrf_compute_geocentric_batch_varying_time(
    const igrf_model_t *model,
    const igrf_geocentric_time_point_t *points,
    size_t count,
    igrf_spherical_t *fields)
{
    igrf__work work;
    igrf_status_t status = IGRF_OK;
    size_t i;
    double last_year = 0.0;
    int have_last = 0;

    if (model == NULL || points == NULL || fields == NULL) {
        return IGRF_ERROR_NULL_ARGUMENT;
    }
    if (count == 0) {
        return IGRF_ERROR_INVALID_INPUT;
    }
    for (i = 0; i < count; i++) {
        igrf_geocentric_t p;

        p.r_km = points[i].r_km;
        p.theta_deg = points[i].theta_deg;
        p.phi_deg = points[i].phi_deg;
        status = igrf__validate_geocentric(&p);
        if (status != IGRF_OK) {
            return status;
        }
        if (!isfinite(points[i].decimal_year)) {
            return IGRF_ERROR_INVALID_INPUT;
        }
        if (points[i].decimal_year < model->epochs[0] ||
            points[i].decimal_year >
                model->epochs[model->n_epochs - 1]) {
            return IGRF_ERROR_YEAR_OUT_OF_RANGE;
        }
    }
    if (igrf__work_alloc(&work, model->n_max) != 0) {
        return IGRF_ERROR_INVALID_MODEL; /* out of memory */
    }

    for (i = 0; i < count; i++) {
        igrf_geocentric_t p;

        if (!have_last || points[i].decimal_year != last_year) {
            status = igrf__prepare_coefficients(model, points[i].decimal_year,
                                                work.g, work.h);
            if (status != IGRF_OK) {
                break;
            }
            last_year = points[i].decimal_year;
            have_last = 1;
        }
        p.r_km = points[i].r_km;
        p.theta_deg = points[i].theta_deg;
        p.phi_deg = points[i].phi_deg;
        igrf__eval_field(model->n_max, work.g, work.h, work.P, work.dP,
                         &p, &fields[i]);
    }

    igrf__work_free(&work);
    return status;
}

igrf_status_t igrf_compute_geodetic_batch_varying_time(
    const igrf_model_t *model,
    const igrf_geodetic_time_point_t *points,
    size_t count,
    igrf_ned_t *fields)
{
    igrf__work work;
    igrf_status_t status = IGRF_OK;
    size_t i;
    double last_year = 0.0;
    int have_last = 0;

    if (model == NULL || points == NULL || fields == NULL) {
        return IGRF_ERROR_NULL_ARGUMENT;
    }
    if (count == 0) {
        return IGRF_ERROR_INVALID_INPUT;
    }
    for (i = 0; i < count; i++) {
        igrf_geodetic_t p;
        igrf_status_t st;

        p.lon_deg = points[i].lon_deg;
        p.lat_deg = points[i].lat_deg;
        p.height_km = points[i].height_km;
        st = igrf__validate_geodetic(&p);
        if (st != IGRF_OK) {
            return st;
        }
        if (!isfinite(points[i].decimal_year)) {
            return IGRF_ERROR_INVALID_INPUT;
        }
        if (points[i].decimal_year < model->epochs[0] ||
            points[i].decimal_year >
                model->epochs[model->n_epochs - 1]) {
            return IGRF_ERROR_YEAR_OUT_OF_RANGE;
        }
    }
    if (igrf__work_alloc(&work, model->n_max) != 0) {
        return IGRF_ERROR_INVALID_MODEL; /* out of memory */
    }

    for (i = 0; i < count; i++) {
        if (!have_last || points[i].decimal_year != last_year) {
            status = igrf__prepare_coefficients(model, points[i].decimal_year,
                                                work.g, work.h);
            if (status != IGRF_OK) {
                break;
            }
            last_year = points[i].decimal_year;
            have_last = 1;
        }
        {
            igrf_geocentric_t gc;
            igrf_spherical_t spherical;

            igrf__geodetic_position_to_geocentric(points[i].lat_deg,
                                                  points[i].height_km,
                                                  points[i].lon_deg,
                                                  &gc.theta_deg, &gc.r_km,
                                                  &gc.phi_deg);
            igrf__eval_field(model->n_max, work.g, work.h, work.P, work.dP,
                             &gc, &spherical);
            igrf__spherical_to_ned(spherical.Br_nT, spherical.Btheta_nT,
                                   spherical.Bphi_nT, points[i].lat_deg,
                                   gc.theta_deg, &fields[i]);
        }
    }

    igrf__work_free(&work);
    return status;
}

/* ---------------------------------------------------------------- */
/* Public API: version                                               */
/* ---------------------------------------------------------------- */

const char *igrf_library_version(void)
{
    return IGRF_VERSION;
}
