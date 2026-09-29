/*
 * peb/test/test_init.c - PEB_$INIT (0x00E31D0C) constant cells
 *
 * The regression this file exists for (source-fzke): io_$probe's two
 * arguments are `pea (d,PC)` cells in the PEB_ code segment -
 *
 *   0x00E31D7E  pea (0x4e,PC)  -> 0x00E31DCE, image bytes 00 01
 *   0x00E31D7A  pea (0x54,PC)  -> 0x00E31DD0, image bytes 00 ff 70 00
 *
 * - and the tree used to pass the first as the raw literal pointer
 * (void *)0xE31DCE and the second through a `PTR_`-prefixed placeholder
 * object.  Both are now named file-statics holding the image values.
 *
 * Also covered: the MMU_$INSTALL page/VA/flag constants, the two exception
 * vectors, and the 58 x 7 longword clear of the per-process FP state.
 */

#include <stdio.h>
#include <string.h>

#include "peb/peb_internal.h"

/* ==========================================================================
 * Test framework
 * ========================================================================== */

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  Running %-44s ", #name); \
    test_##name(); \
    tests_passed++; \
    printf("PASSED\n"); \
} while (0)

#define ASSERT_EQ(expected, actual) do { \
    unsigned long long _e = (unsigned long long)(expected); \
    unsigned long long _a = (unsigned long long)(actual); \
    if (_e != _a) { \
        printf("FAILED\n    Expected: 0x%llx, Got: 0x%llx at line %d\n", \
               _e, _a, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

#define ASSERT_PTR_EQ(expected, actual) do { \
    const void *_e = (const void *)(expected); \
    const void *_a = (const void *)(actual); \
    if (_e != _a) { \
        printf("FAILED\n    Expected: %p, Got: %p at line %d\n", \
               _e, _a, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

/* ==========================================================================
 * Host storage for the globals PEB_$INIT touches
 * ========================================================================== */

MODULE_DATA_DEFINE(peb_globals_t, PEB_$INFO, 0x00E24C78);
volatile int8_t   M68881_EXISTS;

static uint16_t   host_peb_ctl;
/* The PEB control page stands in for SAU2_PEB_CTL (arch/m68k/sau2/hw.h). */
#define SAU2_PEB_CTL peb_ctl_reg
static volatile uint16_t *peb_ctl_reg = &host_peb_ctl;

/* A small arena the vector writes and PEB_CTL land in.  ARCH_VA_TO_PTR maps a
 * target VA into it, so `*(void (**)(void))ARCH_VA_TO_PTR(0x2C)` is safe. */
static uint8_t host_low_memory[0x100];

/* ==========================================================================
 * Mock bookkeeping
 * ========================================================================== */

#define MAX_CALLS 8

static int      probe_calls;
static void    *probe_type_arg;
static void    *probe_addr_arg;
static void    *probe_result_arg;
static int8_t   probe_return;

static int      install_calls;
static uint32_t install_ppn[MAX_CALLS];
static uint32_t install_va[MAX_CALLS];
static uint32_t install_flags[MAX_CALLS];

static int      remove_calls;
static uint32_t remove_ppn[MAX_CALLS];

static int      ec_init_calls;
static void    *ec_init_arg;

/* ==========================================================================
 * Mocks
 * ========================================================================== */

int8_t io_$probe(void *type, void *addr, void *result)
{
    probe_calls++;
    probe_type_arg   = type;
    probe_addr_arg   = addr;
    probe_result_arg = result;
    return probe_return;
}

void MMU_$INSTALL(uint32_t ppn, uint32_t va, uint32_t flags)
{
    if (install_calls < MAX_CALLS) {
        install_ppn[install_calls]   = ppn;
        install_va[install_calls]    = va;
        install_flags[install_calls] = flags;
    }
    install_calls++;
}

void MMU_$REMOVE(uint32_t ppn)
{
    if (remove_calls < MAX_CALLS) {
        remove_ppn[remove_calls] = ppn;
    }
    remove_calls++;
}

void EC_$INIT(ec_$eventcount_t *ec)
{
    ec_init_calls++;
    ec_init_arg = ec;
}

void FIM_$FLINE(void) { }
void PEB_$INT(void)   { }

/* ==========================================================================
 * Code under test
 * ========================================================================== */

#include "../init.c"

static void reset(void)
{
    memset(&PEB_$INFO, 0, sizeof(PEB_$INFO));
    memset(host_low_memory, 0, sizeof(host_low_memory));
    memset(PEB_$WIRED_DATA_START, 0xA5,
           sizeof(peb_fp_state_t) * PEB_MAX_PROCESSES);
    host_peb_ctl = 0xBEEF;
    M68881_EXISTS = 0;

    probe_calls = 0;
    probe_type_arg = NULL;
    probe_addr_arg = NULL;
    probe_result_arg = NULL;
    probe_return = 0;

    install_calls = 0;
    memset(install_ppn, 0, sizeof(install_ppn));
    memset(install_va, 0, sizeof(install_va));
    memset(install_flags, 0, sizeof(install_flags));
    remove_calls = 0;
    memset(remove_ppn, 0, sizeof(remove_ppn));
    ec_init_calls = 0;
    ec_init_arg = NULL;

    /* the constant cells must survive a run unchanged */
    peb_$const_word_1 = 0x0001;
    peb_probe_addr = 0x00FF7000;

    /* route target VAs (the 0x2C / 0x70 vectors) into our own arena */
    ARCH_HOST_VA_BASE = (uintptr_t)host_low_memory;
}

static void *vector_at(uint32_t va)
{
    return *(void **)ARCH_VA_TO_PTR(va);
}

/* ==========================================================================
 * Tests
 * ========================================================================== */

/* The image bytes at the two `pea (d,PC)` cells. */
TEST(probe_constant_cell_values)
{
    reset();
    ASSERT_EQ(0x0001, peb_$const_word_1);          /* 0x00E31DCE: 00 01 */
    ASSERT_EQ(0x00FF7000, peb_probe_addr);      /* 0x00E31DD0: 00 ff 70 00 */
}

/* 0x00E31D76-0x00E31D88: three arguments, all by address. */
TEST(io_probe_gets_both_cells_by_address)
{
    reset();
    PEB_$INIT();

    ASSERT_EQ(1, probe_calls);
    ASSERT_PTR_EQ(&peb_$const_word_1, probe_type_arg);
    ASSERT_PTR_EQ(&peb_probe_addr, probe_addr_arg);
    ASSERT_EQ(0x0001, *(const uint16_t *)probe_type_arg);
    ASSERT_EQ(0x00FF7000, *(const uint32_t *)probe_addr_arg);
    /* the cells are not modified by the run */
    ASSERT_EQ(0x0001, peb_$const_word_1);
    ASSERT_EQ(0x00FF7000, peb_probe_addr);
}

/* 0x00E31D5E-0x00E31D6C and 0x00E31DAC-0x00E31DBA. */
TEST(mmu_install_constants)
{
    reset();
    probe_return = -1;              /* hardware found */
    PEB_$INIT();

    ASSERT_EQ(2, install_calls);
    ASSERT_EQ(0x2C, install_ppn[0]);
    ASSERT_EQ(0xFF7000, install_va[0]);
    ASSERT_EQ(0x16, install_flags[0]);
    ASSERT_EQ(0x2E, install_ppn[1]);
    ASSERT_EQ(0xFF7800, install_va[1]);
    ASSERT_EQ(0x16, install_flags[1]);
    ASSERT_EQ(0, remove_calls);
}

/* 0x00E31D8C-0x00E31D9A: no hardware, so the control page is unmapped again. */
TEST(no_hardware_removes_the_control_mapping)
{
    reset();
    probe_return = 0;
    PEB_$INIT();

    ASSERT_EQ(1, install_calls);
    ASSERT_EQ(0x2C, install_ppn[0]);
    ASSERT_EQ(1, remove_calls);
    ASSERT_EQ(0x2C, remove_ppn[0]);
    ASSERT_EQ(0, PEB_$INSTALLED);
    ASSERT_PTR_EQ(NULL, vector_at(0x70));
}

/* 0x00E31D9C-0x00E31DC0: vector, flag, WCS mapping, then clear PEB_CTL. */
TEST(hardware_found_installs_vector_and_clears_ctl)
{
    reset();
    probe_return = -1;
    PEB_$INIT();

    ASSERT_PTR_EQ((void *)PEB_$INT, vector_at(0x70));
    ASSERT_EQ(-1, PEB_$INSTALLED);
    ASSERT_EQ(0, host_peb_ctl);
}

/* 0x00E31D20-0x00E31D3A: an MC68881 short-circuits everything else. */
TEST(m68881_path_installs_the_fline_vector_only)
{
    reset();
    M68881_EXISTS = -1;
    PEB_$INIT();

    ASSERT_PTR_EQ((void *)FIM_$FLINE, vector_at(0x2C));
    ASSERT_EQ(-1, PEB_$M68881_SAVE_FLAG);
    ASSERT_EQ(0, probe_calls);
    ASSERT_EQ(0, install_calls);
    ASSERT_EQ(0, remove_calls);
    /* but the eventcount is initialised before the test */
    ASSERT_EQ(1, ec_init_calls);
}

/* 0x00E31D12-0x00E31D1E: EC_$INIT runs first, on the PEB eventcount. */
TEST(eventcount_is_initialised_first)
{
    reset();
    PEB_$INIT();

    ASSERT_EQ(1, ec_init_calls);
    ASSERT_PTR_EQ(&PEB_$EVENTCOUNT, ec_init_arg);
}

/* 0x00E31D3E-0x00E31D5C: 58 slots of seven longwords each. */
TEST(fp_state_is_cleared)
{
    int i;
    const uint8_t *p = (const uint8_t *)PEB_$WIRED_DATA_START;

    reset();
    PEB_$INIT();

    ASSERT_EQ(58, PEB_MAX_PROCESSES);
    ASSERT_EQ(7, PEB_FP_STATE_LONGS);
    ASSERT_EQ(0x1C, PEB_FP_STATE_LONGS * 4);
    for (i = 0; i < PEB_MAX_PROCESSES * PEB_FP_STATE_LONGS * 4; i++) {
        ASSERT_EQ(0, p[i]);
    }
}

int main(void)
{
    printf("PEB_$INIT tests\n");
    RUN_TEST(probe_constant_cell_values);
    RUN_TEST(io_probe_gets_both_cells_by_address);
    RUN_TEST(mmu_install_constants);
    RUN_TEST(no_hardware_removes_the_control_mapping);
    RUN_TEST(hardware_found_installs_vector_and_clears_ctl);
    RUN_TEST(m68881_path_installs_the_fline_vector_only);
    RUN_TEST(eventcount_is_initialised_first);
    RUN_TEST(fp_state_is_cleared);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
