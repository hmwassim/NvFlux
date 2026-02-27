/*
 * test_nvflux.c - unit tests (no root, no GPU required)
 *
 * Tests cover nvflux_parse_clocks (the only pure-logic function exposed
 * publicly) and the state read/write round-trip.
 *
 * Each test block is isolated in its own scope so variable names do not
 * collide and failures are pinpointed by the message string.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "nvflux.h"
#include "state.h"  /* state_write / state_read */

static int g_errors = 0;

static void check(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        g_errors++;
    }
}

/* ── nvflux_parse_clocks ─────────────────────────────────────────────────── */

static void test_parse_clocks(void) {
    /* basic: unsorted input → descending output */
    {
        int c[8] = {0};
        int n = nvflux_parse_clocks("3000\n7000,5000\n", c, 8);
        check(n == 3,          "basic: count == 3");
        check(c[0] == 7000,    "basic: c[0] == 7000 (max first)");
        check(c[1] == 5000,    "basic: c[1] == 5000");
        check(c[2] == 3000,    "basic: c[2] == 3000 (min last)");
    }

    /* empty input */
    {
        int c[4] = {0};
        int n = nvflux_parse_clocks("", c, 4);
        check(n == 0, "empty: count == 0");
    }

    /* spaces and mixed separators */
    {
        int c[4] = {0};
        int n = nvflux_parse_clocks("  100 , 200\n 50", c, 4);
        check(n == 3,        "mixed-sep: count == 3");
        check(c[0] == 200,   "mixed-sep: c[0] == 200");
        check(c[2] == 50,    "mixed-sep: c[2] == 50");
    }

    /* single value */
    {
        int c[4] = {0};
        int n = nvflux_parse_clocks("9999", c, 4);
        check(n == 1,          "single: count == 1");
        check(c[0] == 9999,    "single: c[0] == 9999");
    }

    /* max cap */
    {
        int c[2] = {0};
        int n = nvflux_parse_clocks("1 2 3 4 5", c, 2);
        check(n == 2, "cap: count capped at max=2");
    }

    /* all identical values */
    {
        int c[4] = {0};
        int n = nvflux_parse_clocks("500 500 500", c, 4);
        check(n == 3,        "identical: count == 3");
        check(c[0] == 500,   "identical: c[0] == 500");
        check(c[2] == 500,   "identical: c[2] == 500");
    }
}

/* ── state_write / state_read round-trip ─────────────────────────────────── */

static void test_state_roundtrip(void) {
    uid_t uid = getuid();

    /* write "performance", read it back */
    state_write(uid, "performance");
    {
        char buf[NVFLUX_STATE_MAX] = {0};
        int ok = state_read(uid, buf, sizeof(buf));
        check(ok == 1,                          "state: read returns 1");
        check(strcmp(buf, "performance") == 0,  "state: value == 'performance'");
    }

    /* overwrite with "auto", verify the value changed */
    state_write(uid, "auto");
    {
        char buf[NVFLUX_STATE_MAX] = {0};
        int ok = state_read(uid, buf, sizeof(buf));
        check(ok == 1,                    "state: overwrite returns 1");
        check(strcmp(buf, "auto") == 0,   "state: overwritten value == 'auto'");
    }
}

/* ── main ────────────────────────────────────────────────────────────────── */

int main(void) {
    test_parse_clocks();
    test_state_roundtrip();

    if (g_errors == 0)
        printf("All tests passed.\n");
    else
        printf("%d test(s) failed.\n", g_errors);

    return g_errors ? EXIT_FAILURE : EXIT_SUCCESS;
}
