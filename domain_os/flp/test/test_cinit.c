/*
 * flp/test/test_cinit.c - FLP_$CINIT (0x00E3E002)
 *
 * io_$probe, EC_$INIT, SHAKE and DISK_$REGISTER are mocked; the FDC status
 * byte is a plain cell in a VA arena.  Checks the probe arguments, the
 * controller-table slot, the drain loop's two SHAKE shapes and its 201-pass
 * budget, SPECIFY, and the DISK_$REGISTER argument cells.
 */

#include <stdio.h>
#include <string.h>

#include "flp/flp_internal.h"

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
 * Mocks
 * ========================================================================== */

static uint8_t va_arena[0x400];
#define REGS_VA 0x100
#define REGS ((volatile flp_regs_t *)(va_arena + REGS_VA))

static int     probe_calls;
static void   *probe_type, *probe_addr;
static int8_t  probe_result;

int8_t io_$probe(void *type, void *addr, void *result)
{
    (void)result;
    probe_calls++;
    probe_type = type;
    probe_addr = addr;
    return probe_result;
}

static int                ec_init_calls;
static ec_$eventcount_t  *ec_init_ec;

void EC_$INIT(ec_$eventcount_t *ec)
{
    ec_init_calls++;
    ec_init_ec = ec;
}

static int        shake_calls;
static uint16_t  *shake_data[4];
static uint16_t   shake_word[4];
static int16_t   *shake_count_ptr[4];
static int16_t   *shake_dir_ptr[4];
static status_$t  shake_status_after;       /* returned by every call */
static int        shake_clears_busy_after;  /* call index after which the
                                               status byte reads idle */

status_$t SHAKE(uint16_t *data, int16_t *count_ptr, int16_t *dir_ptr)
{
    int n = shake_calls++;
    if (n < 4) {
        shake_data[n] = data;
        shake_word[n] = *data;
        shake_count_ptr[n] = count_ptr;
        shake_dir_ptr[n] = dir_ptr;
    }
    if (shake_clears_busy_after >= 0 && shake_calls >= shake_clears_busy_after) {
        REGS->status = 0;
    }
    return shake_status_after;
}

static int        reg_calls;
static uint16_t  *reg_type, *reg_ctlr, *reg_units, *reg_flags;
static void     **reg_jt;
static uint16_t   reg_type_val, reg_ctlr_val;

uint8_t DISK_$REGISTER(uint16_t *type, uint16_t *controller, uint16_t *units,
                       uint16_t *flags, void **jump_table)
{
    reg_calls++;
    reg_type = type;
    reg_ctlr = controller;
    reg_units = units;
    reg_flags = flags;
    reg_jt = jump_table;
    reg_type_val = *type;
    reg_ctlr_val = *controller;
    return 0;
}

/* ==========================================================================
 * Code under test
 * ========================================================================== */

#include "../flp_data.c"
#include "../cinit.c"

static dcte_t dcte;

static void reset(void)
{
    memset(va_arena, 0, sizeof(va_arena));
    memset(&dcte, 0, sizeof(dcte));
    dcte.cnum = 1;
    dcte.disk_dinit = REGS_VA;
    dcte.disk_error_que = 0x00190000u;  /* the word at +0x3C is 0x0019 */
    memset(FLP_DATA.ctlr_table, 0, sizeof(FLP_DATA.ctlr_table));
    FLP_DATA.hw_addr = 0;

    probe_calls = 0;
    probe_result = -1;
    ec_init_calls = 0;
    shake_calls = 0;
    shake_status_after = status_$ok;
    shake_clears_busy_after = -1;
    reg_calls = 0;
}

/* ==========================================================================
 * Tests
 * ========================================================================== */

/* 0x00E3E014-0x00E3E034: probe arguments; a false result stops here. */
TEST(probe_arguments_and_failure)
{
    reset();
    probe_result = 0;
    ASSERT_EQ(status_$io_controller_not_in_system, FLP_$CINIT(&dcte));
    ASSERT_EQ(1, probe_calls);
    ASSERT_PTR_EQ(&flp_word_zero, probe_type);
    ASSERT_PTR_EQ(&dcte.disk_dinit, probe_addr);
    ASSERT_EQ(0, ec_init_calls);
    ASSERT_EQ(0, FLP_DATA.hw_addr);     /* nothing recorded */
}

/* 0x00E3E038-0x00E3E060: the slot for controller 1 and the eventcount. */
TEST(slot_and_eventcount)
{
    reset();
    ASSERT_EQ(status_$ok, FLP_$CINIT(&dcte));
    ASSERT_EQ(REGS_VA, FLP_DATA.hw_addr);
    ASSERT_EQ(0, FLP_DATA.ctlr_table[0].dcte_va);
    ASSERT_EQ(ARCH_PTR_TO_VA(&dcte), FLP_DATA.ctlr_table[1].dcte_va);
    ASSERT_EQ(REGS_VA, FLP_DATA.ctlr_table[1].hw_addr);
    ASSERT_EQ(1, ec_init_calls);
    ASSERT_PTR_EQ(&FLP_$EC, ec_init_ec);
}

/* An idle FDC: the only handshake is SPECIFY (three words, written). */
TEST(idle_fdc_goes_straight_to_specify)
{
    reset();
    ASSERT_EQ(status_$ok, FLP_$CINIT(&dcte));
    ASSERT_EQ(1, shake_calls);
    ASSERT_PTR_EQ(FLP_DATA.specify_cmd, shake_data[0]);
    ASSERT_PTR_EQ(&flp_word_three, shake_count_ptr[0]);
    ASSERT_PTR_EQ(&flp_word_one, shake_dir_ptr[0]);
    ASSERT_EQ(0x0003, shake_word[0]);
}

/* 0x00E3E070-0x00E3E084: busy with DIO set -> read three words into
 * FLP_$SREGS. */
TEST(busy_with_results_pending_reads_them)
{
    reset();
    REGS->status = FLP_STATUS_RQM | FLP_STATUS_DIO | 0x10;
    shake_clears_busy_after = 1;
    ASSERT_EQ(status_$ok, FLP_$CINIT(&dcte));
    ASSERT_EQ(2, shake_calls);
    ASSERT_PTR_EQ(FLP_$SREGS, shake_data[0]);
    ASSERT_PTR_EQ(&flp_word_three, shake_count_ptr[0]);
    ASSERT_PTR_EQ(&flp_word_zero, shake_dir_ptr[0]);
    ASSERT_PTR_EQ(FLP_DATA.specify_cmd, shake_data[1]);
}

/* 0x00E3E086-0x00E3E092: busy with DIO clear -> write the word 8, with the
 * one cell 0x00E3E110 as both count and direction. */
TEST(busy_without_results_sends_sense_interrupt)
{
    reset();
    REGS->status = FLP_STATUS_RQM | 0x01;
    shake_clears_busy_after = 1;
    ASSERT_EQ(status_$ok, FLP_$CINIT(&dcte));
    ASSERT_EQ(2, shake_calls);
    ASSERT_EQ(8, shake_word[0]);
    ASSERT_PTR_EQ(&flp_word_one, shake_count_ptr[0]);
    ASSERT_PTR_EQ(&flp_word_one, shake_dir_ptr[0]);
}

/* 0x00E3E09E-0x00E3E0AE: 201 passes, then controller error, no SPECIFY. */
TEST(fdc_that_never_goes_idle)
{
    reset();
    REGS->status = FLP_STATUS_RQM | 0x01;
    ASSERT_EQ(status_$disk_controller_error, FLP_$CINIT(&dcte));
    ASSERT_EQ(201, shake_calls);
    ASSERT_EQ(0, reg_calls);
}

/* 0x00E3E0D6-0x00E3E0DA: a SPECIFY handshake failure is the result. */
TEST(specify_failure_is_returned)
{
    reset();
    shake_status_after = status_$disk_controller_timeout;
    ASSERT_EQ(status_$disk_controller_timeout, FLP_$CINIT(&dcte));
    ASSERT_EQ(0, reg_calls);
}

/* 0x00E3E0DC-0x00E3E0FA: DISK_$REGISTER's five by-reference arguments. */
TEST(disk_register_arguments)
{
    reset();
    ASSERT_EQ(status_$ok, FLP_$CINIT(&dcte));
    ASSERT_EQ(1, reg_calls);
    ASSERT_PTR_EQ(&flp_word_one, reg_type);
    ASSERT_EQ(1, reg_type_val);
    ASSERT_EQ(1, reg_ctlr_val);
    ASSERT_PTR_EQ(&FLP_DATA.unit_count, reg_units);
    ASSERT_PTR_EQ(&dcte.disk_error_que, reg_flags);
    ASSERT_EQ(ARCH_PTR_TO_VA(&FLP_DATA), *(uint32_t *)reg_jt);
}

int main(void)
{
    ARCH_HOST_VA_BASE = (uintptr_t)va_arena;

    printf("FLP_$CINIT tests\n");
    RUN_TEST(probe_arguments_and_failure);
    RUN_TEST(slot_and_eventcount);
    RUN_TEST(idle_fdc_goes_straight_to_specify);
    RUN_TEST(busy_with_results_pending_reads_them);
    RUN_TEST(busy_without_results_sends_sense_interrupt);
    RUN_TEST(fdc_that_never_goes_idle);
    RUN_TEST(specify_failure_is_returned);
    RUN_TEST(disk_register_arguments);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
