/*
 * shc.c — reading of IGRF .shc spherical harmonic coefficient files.
 *
 * File format (as used by the IAGA-VMOD / ppigrf reference
 * implementation):
 *
 *   # comment lines (skipped)
 *   n_min n_max n_times sp_order n_steps [min_year max_year]
 *   epoch_1 epoch_2 ... epoch_n_times        (decimal years)
 *   n m c_1 c_2 ... c_n_times                (one line per coefficient)
 *
 * Lines with m >= 0 carry the g (cosine) coefficients, lines with
 * m < 0 the h (sine) coefficient of order -m.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>

#include "igrf_internal.h"

/* Return a pointer to the next whitespace-delimited token in *cursor
 * (NUL-terminating it), or NULL when the line is exhausted. */
static char *shc_next_token(char **cursor)
{
    char *p = *cursor;
    char *start;

    while (*p != '\0' && isspace((unsigned char)*p)) {
        p++;
    }
    if (*p == '\0') {
        *cursor = p;
        return NULL;
    }
    start = p;
    while (*p != '\0' && !isspace((unsigned char)*p)) {
        p++;
    }
    if (*p != '\0') {
        *p = '\0';
        p++;
    }
    *cursor = p;
    return start;
}

/* Parse a complete `long` token; returns 0 on success. */
static int shc_parse_long(const char *token, long *out)
{
    char *end;
    long value;

    if (token == NULL || *token == '\0') {
        return -1;
    }
    value = strtol(token, &end, 10);
    if (*end != '\0' || end == token) {
        return -1;
    }
    *out = value;
    return 0;
}

/* Parse a complete finite `double` token; returns 0 on success. */
static int shc_parse_double(const char *token, double *out)
{
    char *end;
    double value;

    if (token == NULL || *token == '\0') {
        return -1;
    }
    value = strtod(token, &end);
    if (*end != '\0' || end == token || !isfinite(value)) {
        return -1;
    }
    *out = value;
    return 0;
}

/* Read one line into buf.  Returns 1 on success, 0 at EOF.  A line
 * longer than cap is split; the resulting parse error is reported
 * by the caller's validation. */
static int shc_read_line(FILE *fp, char *buf, size_t cap)
{
    return fgets(buf, (int)cap, fp) != NULL;
}

/* A data line is any line containing non-whitespace (blank lines are
 * skipped by the callers, like the reference implementation skips
 * them implicitly through its token parsing). */
static int shc_line_is_data(const char *line)
{
    while (*line != '\0' && isspace((unsigned char)*line)) {
        line++;
    }
    return *line != '\0';
}

static void shc_free_model(struct igrf_model *model)
{
    if (model != NULL) {
        free(model->epochs);
        free(model->g);
        free(model->h);
        free(model);
    }
}

/*
 * Load a model from filename.  Returns NULL if the file cannot be
 * opened or does not contain a valid .shc model.
 */
igrf_model_t *igrf_model_load(const char *filename)
{
    FILE *fp = NULL;
    struct igrf_model *model = NULL;
    char header_buf[1024];
    char *line = NULL;
    size_t line_cap;
    long n_min = 0, n_max = 0, n_times = 0;
    size_t nm;
    unsigned char *seen = NULL;
    double prev_epoch = 0.0;
    long i;
    size_t missing = 0;

    if (filename == NULL) {
        return NULL;
    }
    fp = fopen(filename, "r");
    if (fp == NULL) {
        return NULL;
    }

    /* --- header line: skip comment lines, then read 5 integers --- */
    for (;;) {
        char *cursor;
        char *tok;

        if (!shc_read_line(fp, header_buf, sizeof(header_buf))) {
            goto fail; /* no header at all */
        }
        if (header_buf[0] == '#') {
            continue;
        }
        /* skip a UTF-8 BOM if the file happens to have one */
        if ((unsigned char)header_buf[0] == 0xEF &&
            (unsigned char)header_buf[1] == 0xBB &&
            (unsigned char)header_buf[2] == 0xBF &&
            header_buf[3] == '#') {
            continue;
        }
        if (!shc_line_is_data(header_buf)) {
            continue; /* blank line */
        }

        cursor = header_buf;
        if (shc_parse_long(shc_next_token(&cursor), &n_min) != 0 ||
            shc_parse_long(shc_next_token(&cursor), &n_max) != 0 ||
            shc_parse_long(shc_next_token(&cursor), &n_times) != 0) {
            goto fail;
        }
        /* remaining header tokens (spatial order, steps, year range)
         * are informational and ignored, like in the reference */
        tok = shc_next_token(&cursor);
        (void)tok;
        break;
    }

    if (n_min < 1 ||
        n_max < n_min || n_max > IGRF_MAX_MODEL_DEGREE ||
        n_times < 1 || n_times > IGRF_MAX_MODEL_EPOCHS) {
        goto fail;
    }

    model = (struct igrf_model *)calloc(1, sizeof(*model));
    if (model == NULL) {
        goto fail;
    }
    model->n_max = (int)n_max;
    model->n_epochs = (int)n_times;
    nm = IGRF_NM(model->n_max);

    model->epochs = (double *)calloc((size_t)n_times, sizeof(double));
    model->g = (double *)calloc((size_t)n_times * nm, sizeof(double));
    model->h = (double *)calloc((size_t)n_times * nm, sizeof(double));
    /* one flag byte per coefficient slot: bit 0 = g seen, bit 1 = h seen */
    seen = (unsigned char *)calloc(nm, sizeof(unsigned char));
    if (model->epochs == NULL || model->g == NULL || model->h == NULL ||
        seen == NULL) {
        goto fail;
    }

    /* line buffer sized for the widest possible data line:
     * "n m" + n_times coefficients, with margin */
    line_cap = ((size_t)n_times + 4) * 40 + 64;
    line = (char *)malloc(line_cap);
    if (line == NULL) {
        goto fail;
    }

    /* --- epoch line --- */
    for (;;) {
        char *cursor;
        long k = 0;

        if (!shc_read_line(fp, line, line_cap)) {
            goto fail; /* epoch line missing */
        }
        if (line[0] == '#' || !shc_line_is_data(line)) {
            continue;
        }
        cursor = line;
        for (k = 0; k < n_times; k++) {
            if (shc_parse_double(shc_next_token(&cursor),
                                 &model->epochs[k]) != 0) {
                goto fail;
            }
        }
        if (shc_next_token(&cursor) != NULL) {
            goto fail; /* too many epochs on the line */
        }
        break;
    }
    for (i = 0; i < n_times; i++) {
        if (i > 0 && model->epochs[i] <= prev_epoch) {
            goto fail; /* epochs must be strictly ascending */
        }
        prev_epoch = model->epochs[i];
    }

    /* --- coefficient lines --- */
    for (;;) {
        char *cursor;
        long n, m, k;
        long col;
        int is_h;

        if (!shc_read_line(fp, line, line_cap)) {
            break; /* EOF: end of coefficient data */
        }
        if (line[0] == '#' || !shc_line_is_data(line)) {
            continue;
        }

        cursor = line;
        if (shc_parse_long(shc_next_token(&cursor), &n) != 0 ||
            shc_parse_long(shc_next_token(&cursor), &m) != 0) {
            goto fail;
        }
        if (n < n_min || n > n_max) {
            goto fail;
        }
        if (m >= 0) {
            if (m > n) {
                goto fail;
            }
            is_h = 0;
            col = m;
        } else {
            if (-m > n) {
                goto fail;
            }
            is_h = 1;
            col = -m;
        }

        {
            unsigned char flag = (unsigned char)(is_h ? 2 : 1);
            unsigned char *slot = &seen[IGRF_IDX(n, col, model->n_max)];
            if (*slot & flag) {
                goto fail; /* duplicate coefficient */
            }
            *slot |= flag;
        }

        for (k = 0; k < n_times; k++) {
            double value;
            double *dest = is_h ? model->h : model->g;

            if (shc_parse_double(shc_next_token(&cursor), &value) != 0) {
                goto fail;
            }
            dest[(size_t)k * nm + IGRF_IDX(n, col, model->n_max)] = value;
        }
        if (shc_next_token(&cursor) != NULL) {
            goto fail; /* more values than epochs */
        }
    }

    /* --- completeness: every (n, m) of the expansion must be present --- */
    for (long n = n_min; n <= n_max; n++) {
        for (long m = 0; m <= n; m++) {
            if (!(seen[IGRF_IDX(n, m, model->n_max)] & 1)) {
                missing = 1;
            }
            if (m > 0 && !(seen[IGRF_IDX(n, m, model->n_max)] & 2)) {
                missing = 1;
            }
        }
    }
    if (missing) {
        goto fail;
    }

    free(seen);
    free(line);
    fclose(fp);
    return (igrf_model_t *)model;

fail:
    free(seen);
    free(line);
    if (fp != NULL) {
        fclose(fp);
    }
    shc_free_model(model);
    return NULL;
}

void igrf_model_free(igrf_model_t *model)
{
    shc_free_model((struct igrf_model *)model);
}
