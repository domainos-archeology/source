/*
 * io/test/test_init.c - unit tests for IO_$INIT (0x00E328E0) and
 * io_$build_dcte_list (0x00E32834)
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <setjmp.h>

static int tests_passed = 0;
static int tests_failed = 0;

static void reset_state(void);

#define RUN_TEST(name) do { \
    printf("  Running %s... ", #name); \
    reset_state(); \
    test_##name(); \
    printf("PASSED\n"); \
    tests_passed++; \
} while (0)

#define ASSERT_EQ(expected, actual) do { \
    if ((unsigned long)(expected) != (unsigned long)(actual)) { \
        printf("FAILED\n    Expected: 0x%lx, Got: 0x%lx at line %d\n", \
               (unsigned long)(expected), (unsigned long)(actual), __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

#include "io/io_internal.h"

/* ---- data ---- */
dcte_t *IO_$DCTE_LIST;
io_$bus_epv_t IO_$BUS_EPV[IO_BUS_EPV_COUNT];
ml_$exclusion_t io_$exclusion;
ml_$exclusion_t IO_$WIRING_EXCLUSION;
uint32_t io_$dcte_area_end;
uint32_t io_$dcte_area_start;
int8_t IO_$IN_INIT;
MODULE_DATA_DEFINE(io_$dctes_t, DCTES, 0x00E2C8BC);

static uint8_t area[0x100] __attribute__((aligned(16)));
static dcte_t sd1, sd2;

/* ---- mocks ---- */
static char log_buf[256];
static void note(const char *s) { strcat(log_buf, s); }
static int n_excl, n_crash;
static status_$t crash_status, vtop_status;
static uint32_t vtop_va;
static jmp_buf crash_jmp;
static int8_t in_init_seen;

void ML_$EXCLUSION_INIT(ml_$exclusion_t *e) { (void)e; n_excl++; }
void DMA_$INIT(void) { note("D"); in_init_seen = IO_$IN_INIT; }
uint32_t MMU_$VTOP(uint32_t va, status_$t *status) { vtop_va = va; *status = vtop_status; return 0x123; }
void CRASH_SYSTEM(const status_$t *s) { n_crash++; crash_status = *s; longjmp(crash_jmp, 1); }
void VFMT_$WRITE10(const char *format, ...)
{
    if (format[0] == 'D') note("[dev]");
    else if (format[0] == 'O') note("[ok]");
    else note("[fail]");
}
static void bus0(void) { note("b0"); }
static void bus4(void) { note("b4"); }
static status_$t init_ok(dcte_t *d) { (void)d; note("i"); return 0; }
static status_$t init_bad(dcte_t *d) { (void)d; note("x"); return 0x00100004; }

#include "../build_dcte_list.c"
#include "../init.c"

static void reset_state(void)
{
    memset(area, 0, sizeof(area));
    memset(&sd1, 0, sizeof(sd1));
    memset(&sd2, 0, sizeof(sd2));
    memset(IO_$BUS_EPV, 0, sizeof(IO_$BUS_EPV));
    memset(DEV_DCTES, 0, sizeof(DEV_DCTES));
    ARCH_HOST_VA_BASE = (uintptr_t)area - 0x20000u;
    io_$dcte_area_start = io_$dcte_area_end = 0x20000u;
    IO_$DCTE_LIST = (dcte_t *)(uintptr_t)1;
    log_buf[0] = 0;
    n_excl = n_crash = 0;
    vtop_status = 0;
    vtop_va = 0;
}

static void test_static_list_only(void)
{
    int8_t verbose = 0;
    status_$t st = -1;
    DEV_DCTES[0] = ARCH_PTR_TO_VA(&sd1);
    DEV_DCTES[1] = ARCH_PTR_TO_VA(&sd2);
    sd1.csrsytr = init_ok;
    sd1.nextp = &sd2;                   /* overwritten */
    IO_$BUS_EPV[0].init = bus0;
    IO_$BUS_EPV[4].init = bus4;
    IO_$INIT(NULL, &verbose, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(2, n_excl);
    ASSERT_EQ(0xFF, (uint8_t)in_init_seen);
    ASSERT_EQ(0, IO_$IN_INIT);
    ASSERT_EQ((uintptr_t)&sd1, (uintptr_t)IO_$DCTE_LIST);
    ASSERT_EQ((uintptr_t)&sd2, (uintptr_t)sd1.nextp);
    ASSERT_EQ(0, (uintptr_t)sd2.nextp);
    ASSERT_EQ(0, strcmp(log_buf, "Db0b4i"));
}

static void test_verbose_and_failure(void)
{
    int8_t verbose = (int8_t)0xFF;
    status_$t st = -1;
    DEV_DCTES[0] = ARCH_PTR_TO_VA(&sd1);
    DEV_DCTES[1] = ARCH_PTR_TO_VA(&sd2);
    sd1.name[0] = 'W'; sd1.csrsytr = init_bad;
    sd2.name[0] = 'R'; sd2.csrsytr = init_ok;
    IO_$INIT(NULL, &verbose, &st);
    ASSERT_EQ(0x00100004, sd1.cstatus);
    ASSERT_EQ(0, sd2.cstatus);
    ASSERT_EQ(0, strcmp(log_buf, "D[dev]x[fail][dev]i[ok]"));
}

static void test_kind1_translation_and_dbf_ppn(void)
{
    int8_t verbose = 0;
    status_$t st;
    DEV_DCTES[0] = ARCH_PTR_TO_VA(&sd1);
    DEV_DCTES[1] = ARCH_PTR_TO_VA(&sd2);
    sd1.kind = 1; sd1.io_va = 0;        /* keeps D2 = 0xFFFF */
    sd2.kind = 1; sd2.io_va = 0xFF9C12;
    IO_$INIT(NULL, &verbose, &st);
    ASSERT_EQ(0xFFFFu << 10, sd1.io_pa);
    ASSERT_EQ(0xFF9C12, vtop_va);
    ASSERT_EQ((0x123u << 10) + 0x12, sd2.io_pa);
}

static void test_vtop_failure_crashes(void)
{
    int8_t verbose = 0;
    status_$t st;
    DEV_DCTES[0] = ARCH_PTR_TO_VA(&sd1);
    sd1.kind = 1; sd1.io_va = 0x1000;
    vtop_status = 0x00040004;
    if (setjmp(crash_jmp) == 0) {
        IO_$INIT(NULL, &verbose, &st);
    }
    ASSERT_EQ(1, n_crash);
    ASSERT_EQ(0x00040004, crash_status);
}

static void test_dynamic_area(void)
{
    dcte_t *a = (dcte_t *)(void *)area;
    dcte_t *b = (dcte_t *)(void *)(area + 0x48);
    dcte_t *c = (dcte_t *)(void *)(area + 0x90);
    a->length = 0x44;                   /* rounded to 0x48, bad length */
    b->length = 0x48;
    c->length = 0;                      /* ends the walk, unlinked */
    io_$dcte_area_end = 0x20000u + 0xE0;
    DEV_DCTES[0] = ARCH_PTR_TO_VA(&sd1);
    io_$build_dcte_list();
    ASSERT_EQ((uintptr_t)a, (uintptr_t)IO_$DCTE_LIST);
    ASSERT_EQ((uintptr_t)b, (uintptr_t)a->nextp);
    ASSERT_EQ((uintptr_t)&sd1, (uintptr_t)b->nextp);
    ASSERT_EQ(0x00100007, a->cstatus);
    ASSERT_EQ(0, b->cstatus);
    ASSERT_EQ(0x00100007, c->cstatus);
}

static void test_area_slack(void)
{
    dcte_t *a = (dcte_t *)(void *)area;
    a->length = 0x48;
    io_$dcte_area_end = 0x20000u + 8;   /* 8 bytes left: not looked at */
    io_$build_dcte_list();
    ASSERT_EQ(0, (uintptr_t)IO_$DCTE_LIST);
}

int main(void)
{
    printf("IO_$INIT tests\n");
    RUN_TEST(static_list_only);
    RUN_TEST(verbose_and_failure);
    RUN_TEST(kind1_translation_and_dbf_ppn);
    RUN_TEST(vtop_failure_crashes);
    RUN_TEST(dynamic_area);
    RUN_TEST(area_slack);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
