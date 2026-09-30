/*
 * io/test/test_dctes_data.c - the DCTES segment (0xE2C8BC, 0xD0 bytes)
 *
 * Pins the image contents of RING_DCTE, FLP_DCTE, WIN_DCTE and DEV_DCTES
 * (the VA cells keep their image values on a host build).
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

#include "io/io_internal.h"
#include "ring/ring.h"
#include "flp/flp.h"
#include "win/win.h"

status_$t RING_$INIT(void *d) { (void)d; return 0; }
int8_t RING_$INT(void *d) { (void)d; return 0; }
status_$t FLP_$CINIT(dcte_t *d) { (void)d; return 0; }
int8_t FLP_$INT(dcte_t *d) { (void)d; return 0; }
status_$t WIN_$CINIT(void *d) { (void)d; return 0; }
int8_t WIN_$INT(dcte_t *d) { (void)d; return 0; }

/* The SAU2 register pages (arch/m68k/sau2/hw.h) the host does not define */
#define SAU2_RING2_BASE 0x00FF9C00u
#define SAU2_DISK_BASE  0x00FFA800u

#include "../dctes_data.c"

TEST(ring_dcte)
{
    ASSERT_EQ(1, RING_DCTE.kind);
    ASSERT_EQ(0x40, RING_DCTE.length);
    ASSERT_EQ(2, RING_DCTE.ctype);
    ASSERT_EQ(0, RING_DCTE.cnum);
    ASSERT_EQ(0, (uintptr_t)RING_DCTE.nextp);
    ASSERT_EQ((uintptr_t)RING_$INIT, (uintptr_t)RING_DCTE.csrsytr);
    ASSERT_EQ(0, RING_DCTE.cstatus);
    ASSERT_EQ(0, RING_DCTE.name[0]);
    ASSERT_EQ(0, RING_DCTE.io_va);
    ASSERT_EQ(0x00FF9C00, RING_DCTE.disk_dinit);
    ASSERT_EQ(0x00E75748, RING_DCTE.disk_do_io);
    ASSERT_EQ(0x0018001B, RING_DCTE.disk_error_que);
}

TEST(flp_and_win_dcte)
{
    ASSERT_EQ(1, FLP_DCTE.ctype);
    ASSERT_EQ((uintptr_t)FLP_$CINIT, (uintptr_t)FLP_DCTE.csrsytr);
    ASSERT_EQ(0x00FFA800, FLP_DCTE.disk_dinit);
    ASSERT_EQ(0x00E19F6C, FLP_DCTE.disk_do_io);
    ASSERT_EQ(0x00170000, FLP_DCTE.disk_error_que);
    ASSERT_EQ(0, WIN_DCTE.ctype);
    ASSERT_EQ(0x40, WIN_DCTE.length);
    ASSERT_EQ((uintptr_t)WIN_$CINIT, (uintptr_t)WIN_DCTE.csrsytr);
    ASSERT_EQ(0x00FFA800, WIN_DCTE.disk_dinit);
    ASSERT_EQ(0x00E19BFA, WIN_DCTE.disk_do_io);
    ASSERT_EQ(0x00170000, WIN_DCTE.disk_error_que);
}

TEST(dev_dctes)
{
    ASSERT_EQ(0x00E2C93C, DEV_DCTES[0]);
    ASSERT_EQ(0x00E2C8FC, DEV_DCTES[1]);
    ASSERT_EQ(0x00E2C8BC, DEV_DCTES[2]);
    ASSERT_EQ(0, DEV_DCTES[3]);
}

int main(void)
{
    printf("DCTES tests:\n");
    RUN_TEST(ring_dcte);
    RUN_TEST(flp_and_win_dcte);
    RUN_TEST(dev_dctes);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
