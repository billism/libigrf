/*
 * test_shc.c — .shc parsing unit tests.  Valid and invalid model file
 * variants are written to temporary files and checked through the
 * public igrf_model_load() API (non-NULL model == parse success).
 */
#include <stdio.h>
#include <string.h>

#include "igrf.h"
#include "testutil.h"

#define TMP "tmp_test_shc.shc"

static void write_file(const char *content)
{
    FILE *fp = fopen(TMP, "wb");
    if (fp == NULL) {
        printf("FAIL test_shc: cannot write %s\n", TMP);
        tt_failures++;
        return;
    }
    fwrite(content, 1, strlen(content), fp);
    fclose(fp);
}

/* A complete, valid degree-2 two-epoch model. */
static const char *VALID_MODEL =
    "# tiny test model\n"
    "1 2 2 2 1 2000.0 2005.0\n"
    "2000.0 2005.0\n"
    " 1   0   -30000 -29000\n"
    " 1   1     2000   1900\n"
    " 1  -1     5000   4800\n"
    " 2   0   -1500  -1600\n"
    " 2   1     2900   2950\n"
    " 2  -1    -2100  -2200\n"
    " 2   2     1700   1650\n"
    " 2  -2     -450   -460\n";

static void expect_reject(const char *content, const char *what)
{
    igrf_model_t *m;

    write_file(content);
    m = igrf_model_load(TMP);
    tt_checks++;
    if (m != NULL) {
        tt_failures++;
        printf("FAIL test_shc: %s was accepted but must be rejected\n",
               what);
        igrf_model_free(m);
    }
}

static void expect_accept(const char *content, const char *what)
{
    igrf_model_t *m;

    write_file(content);
    m = igrf_model_load(TMP);
    tt_checks++;
    if (m == NULL) {
        tt_failures++;
        printf("FAIL test_shc: %s was rejected but must be accepted\n",
               what);
    } else {
        igrf_model_free(m);
    }
}

int main(void)
{
    igrf_model_t *model;
    char buf[2048];

    /* --- the real model files --- */
    model = igrf_model_load(IGRF_TEST_DATA_DIR "/igrf14.shc");
    CHECK(model != NULL, "igrf14.shc must load");
    igrf_model_free(model);
    model = igrf_model_load(IGRF_TEST_DATA_DIR "/igrf13.shc");
    CHECK(model != NULL, "igrf13.shc must load");
    igrf_model_free(model);

    /* --- argument handling --- */
    CHECK(igrf_model_load(NULL) == NULL, "NULL filename returns NULL");
    CHECK(igrf_model_load("no_such_file_abc.shc") == NULL,
          "missing file returns NULL");

    /* --- invalid content is rejected --- */
    expect_reject("", "empty file");
    expect_reject("# only comments\n# nothing else\n", "comments only");
    expect_reject("1 2 2 2 1 2000.0 2005.0\n", "header without epochs");
    expect_reject("1 2 2 2 1 2000.0 2005.0\n2000.0 2005.0\n",
                  "epochs without coefficients");
    expect_reject("abc def\n", "garbage header");

    expect_reject(
        "1 2 2 2 1 2000.0 2005.0\n2000.0 2005.0\n"
        " 1 0 -30000 -29000\n"
        " 1 1 2000 1900\n",
        "missing h coefficient");
    expect_reject(
        "1 1 2 2 1 2000.0 2005.0\n2000.0 2005.0\n"
        " 1 0 -30000 -29000\n"
        " 1 1 2000 1900\n"
        " 1 1 2000 1900\n"
        " 1 -1 5000 4800\n",
        "duplicate coefficient");
    expect_reject(
        "1 1 2 2 1 2000.0 2005.0\n2000.0 2005.0\n"
        " 1 0 -30000 nan\n"
        " 1 1 2000 1900\n"
        " 1 -1 5000 4800\n",
        "NaN coefficient");
    expect_reject(
        "1 1 2 2 1 2000.0 2005.0\n2000.0 2005.0\n"
        " 1 0 -30000 inf\n"
        " 1 1 2000 1900\n"
        " 1 -1 5000 4800\n",
        "Inf coefficient");
    expect_reject(
        "1 2 2 2 1 2000.0 2005.0\n2000.0 1995.0\n"
        " 1 0 -30000 -29000\n"
        " 1 1 2000 1900\n"
        " 1 -1 5000 4800\n"
        " 2 0 -1500 -1600\n"
        " 2 1 2900 2950\n"
        " 2 -1 -2100 -2200\n"
        " 2 2 1700 1650\n"
        " 2 -2 -450 -460\n",
        "descending epochs");
    expect_reject(
        "1 2 2 2 1 2000.0 2005.0\n2000.0 2005.0\n"
        " 1 0 -30000 -29000 42\n"
        " 1 1 2000 1900\n"
        " 1 -1 5000 4800\n"
        " 2 0 -1500 -1600\n"
        " 2 1 2900 2950\n"
        " 2 -1 -2100 -2200\n"
        " 2 2 1700 1650\n"
        " 2 -2 -450 -460\n",
        "too many values on a coefficient line");
    expect_reject(
        "1 2 2 2 1 2000.0 2005.0\n2000.0 2005.0\n"
        " 1 0 -30000\n"
        " 1 1 2000 1900\n"
        " 1 -1 5000 4800\n"
        " 2 0 -1500 -1600\n"
        " 2 1 2900 2950\n"
        " 2 -1 -2100 -2200\n"
        " 2 2 1700 1650\n"
        " 2 -2 -450 -460\n",
        "too few values on a coefficient line");
    expect_reject(
        "1 2 2 2 1 2000.0 2005.0\n2000.0 2005.0\n"
        " 0 0 5 6\n"
        " 1 1 2000 1900\n"
        " 1 -1 5000 4800\n"
        " 2 0 -1500 -1600\n"
        " 2 1 2900 2950\n"
        " 2 -1 -2100 -2200\n"
        " 2 2 1700 1650\n"
        " 2 -2 -450 -460\n",
        "degree 0 line");
    expect_reject(
        "1 2 2 2 1 2000.0 2005.0\n2000.0 2005.0\n"
        " 1 0 -30000 -29000\n"
        " 1 1 2000 1900\n"
        " 1 -1 5000 4800\n"
        " 2 3 1 2\n"
        " 2 0 -1500 -1600\n"
        " 2 1 2900 2950\n"
        " 2 -1 -2100 -2200\n"
        " 2 2 1700 1650\n"
        " 2 -2 -450 -460\n",
        "order higher than degree");
    expect_reject(
        "1 2 2 2 1 2000.0 2005.0\n2000.0 2005.0\n"
        " 3 0 1 2\n",
        "degree beyond n_max");
    expect_reject(
        "1 1 2 2 1 2000.0 2005.0\n2000.0 2005.0\n"
        "garbage text\n"
        " 1 0 -30000 -29000\n"
        " 1 1 2000 1900\n"
        " 1 -1 5000 4800\n",
        "garbage line among coefficients");

    /* --- valid variants are accepted --- */
    expect_accept(VALID_MODEL, "valid minimal model");

    /* same model with CRLF line endings */
    {
        const char *p;
        size_t i = 0;
        for (p = VALID_MODEL; *p != '\0'; p++) {
            if (*p == '\n') {
                buf[i++] = '\r';
            }
            buf[i++] = *p;
        }
        buf[i] = '\0';
    }
    expect_accept(buf, "CRLF line endings");

    expect_accept(
        "# comment\n\n"
        "   1   2   2   2   1   2000.0   2005.0\n"
        "\n"
        "   2000.0   2005.0\n"
        " 1   0   -30000   -29000\n"
        "\n"
        " 1  -1     5000   4800\n"
        " 1   1     2000   1900\n"
        " 2  -2     -450   -460\n"
        " 2   0   -1500  -1600\n"
        " 2  -1    -2100  -2200\n"
        " 2   1     2900   2950\n"
        " 2   2     1700   1650\n",
        "blank lines and shuffled coefficient order");

    /* the loader must not leak the temporary on success either */
    remove(TMP);
    return tt_report("test_shc");
}
