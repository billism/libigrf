/*
 * testutil.h — minimal test helpers for the libigrf test suite.
 * Each test executable is a single translation unit including this
 * header once; failures are counted and the process exits non-zero
 * if any check failed.
 */
#ifndef IGRF_TESTUTIL_H
#define IGRF_TESTUTIL_H

#include <math.h>
#include <stdio.h>

static int tt_checks = 0;
static int tt_failures = 0;

#define CHECK(cond, ...)                                                      \
    do {                                                                      \
        tt_checks++;                                                          \
        if (!(cond)) {                                                        \
            tt_failures++;                                                    \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                       \
            printf(__VA_ARGS__);                                              \
            printf("\n");                                                     \
        }                                                                     \
    } while (0)

/* Check a igrf_status_t expression. */
#define CHECK_STATUS(expr, want)                                              \
    do {                                                                      \
        tt_checks++;                                                          \
        igrf_status_t status_ = (expr);                                       \
        if (status_ != (want)) {                                              \
            tt_failures++;                                                    \
            printf("FAIL %s:%d: %s returned %d, expected %d\n", __FILE__,     \
                   __LINE__, #expr, (int)status_, (int)(want));               \
        }                                                                     \
    } while (0)

/* Check |a - b| <= tol with a diagnostic printout. */
#define CHECK_CLOSE(a, b, tol, what)                                          \
    do {                                                                      \
        tt_checks++;                                                          \
        double a_ = (a), b_ = (b), diff_ = fabs(a_ - b_);                     \
        if (!(diff_ <= (tol))) {                                              \
            tt_failures++;                                                    \
            printf("FAIL %s:%d: %s: %.17g vs %.17g (|diff| = %.3g > %g)\n",   \
                   __FILE__, __LINE__, what, a_, b_, diff_, (double)(tol));   \
        }                                                                     \
    } while (0)

static int tt_report(const char *suite)
{
    printf("[%s] %d checks, %d failures\n", suite, tt_checks, tt_failures);
    return tt_failures == 0 ? 0 : 1;
}

#endif /* IGRF_TESTUTIL_H */
