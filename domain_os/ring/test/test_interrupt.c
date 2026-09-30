/*
 * ring/test/test_interrupt.c - RING_INTERRUPT (0x00E0AB0C): the type-2
 * controller's do_io called with its DCTE.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    int _before = tests_failed; \
    printf("  Running %s... ", #name); \
    fflush(stdout); \
    test_##name(); \
    if (tests_failed == _before) { tests_passed++; printf("PASSED\n"); } \
} while (0)

#define ASSERT_EQ(expected, actual) do { \
    unsigned long _e = (unsigned long)(expected); \
    unsigned long _a = (unsigned long)(actual); \
    if (_e != _a) { \
        printf("FAILED\n    Expected: 0x%lx, Got: 0x%lx at line %d\n", \
               _e, _a, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

#include "ring/ring.h"
#include "io/io.h"

io_int_ctrl_t IO_$INT_CTRL;
static dcte_t *seen;
static int n;
static void do_io(dcte_t *d) { seen = d; n++; }

#include "../interrupt.c"

TEST(dispatch)
{
    static dcte_t ring_dcte;
    IO_$INT_CTRL.type2_do_io = do_io;
    IO_$INT_CTRL.type2_dcte = &ring_dcte;
    RING_INTERRUPT();
    ASSERT_EQ(1, n);
    ASSERT_EQ(1, seen == &ring_dcte);
}

int main(void)
{
    printf("RING_INTERRUPT tests:\n");
    RUN_TEST(dispatch);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
