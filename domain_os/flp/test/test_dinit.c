/*
 * flp/test/test_dinit.c - FLP_$DINIT (0x00E3E112)
 *
 * MMU_$VTOP, WP_$WIRE and EXCS are mocked.  Checks the unit bound, the
 * one-time wiring arithmetic, the RECALIBRATE call, the per-unit resets,
 * the geometry fill-in and the unconditional w_06 = 1.
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

static int       vtop_calls;
static uint32_t  vtop_va;
static uint32_t  vtop_ppn;

uint32_t MMU_$VTOP(uint32_t va, status_$t *status)
{
    vtop_calls++;
    vtop_va = va;
    *status = status_$ok;
    return vtop_ppn;
}

static int       wire_calls;
static uint32_t  wire_ppn;

void WP_$WIRE(uint32_t ppn)
{
    wire_calls++;
    wire_ppn = ppn;
}

static int        excs_calls;
static uint16_t  *excs_cmd;
static int16_t   *excs_count;
static uint16_t   excs_cmd_word1;
static status_$t  excs_status;

static uint16_t   excs_vol_as_options;

status_$t EXCS(uint16_t *cmd, int16_t *count_ptr, disk_$volume_t *vol)
{
    excs_vol_as_options = vol->as_options;
    excs_calls++;
    excs_cmd = cmd;
    excs_count = count_ptr;
    excs_cmd_word1 = cmd[1];
    return excs_status;
}

/* ==========================================================================
 * Code under test
 * ========================================================================== */

#include "../flp_data.c"
#include "../dinit.c"

static int32_t num_blocks;
static uint16_t sec_per_track, num_heads, flags;
static flp_pvlabel_info_t label;

static void reset(void)
{
    memset(va_arena, 0, sizeof(va_arena));
    FLP_DATA.ctlr_table[0].hw_addr = REGS_VA;
    FLP_DATA.ctlr_table[1].hw_addr = REGS_VA + 0x40;
    FLP_DATA.hw_addr = 0;
    FLP_DATA.initialized = 0;
    FLP_DATA.fmt_buf_pa = 0;
    FLP_DATA.cmd_retry = 9;
    FLP_DATA.recal_cmd[1] = 0x7777;
    memset(FLP_DATA.unit_cyl, 0x55, sizeof(FLP_DATA.unit_cyl));
    memset(FLP_DATA.disk_change, -1, sizeof(FLP_DATA.disk_change));

    vtop_calls = 0;
    vtop_ppn = 0x0ABC;
    wire_calls = 0;
    excs_calls = 0;
    excs_status = status_$ok;

    num_blocks = 0;
    sec_per_track = 0xEEEE;
    num_heads = 0xEEEE;
    flags = 0xEEEE;
    memset(&label, 0xEE, sizeof(label));
}

/* ==========================================================================
 * Tests
 * ========================================================================== */

/* 0x00E3E130-0x00E3E13C */
TEST(unit_above_three_is_rejected)
{
    reset();
    ASSERT_EQ(status_$invalid_unit_number,
              FLP_$DINIT(4, 0, &num_blocks, &sec_per_track, &num_heads,
                         &label, &flags));
    ASSERT_EQ(0, excs_calls);
    ASSERT_EQ(0xEEEE, label.w_06);      /* not even the unconditional store */
}

/* 0x00E3E154-0x00E3E18C: MMU_$VTOP of io_buffer, WP_$WIRE of the ppn, and
 * fmt_buf_pa = ppn << 10 | offset-in-page; only the first time. */
TEST(first_call_wires_the_format_buffer)
{
    uint32_t buf_va = ARCH_PTR_TO_VA(FLP_DATA.io_buffer);

    reset();
    ASSERT_EQ(status_$ok,
              FLP_$DINIT(0, 0, &num_blocks, &sec_per_track, &num_heads,
                         &label, &flags));
    ASSERT_EQ(1, vtop_calls);
    ASSERT_EQ(buf_va, vtop_va);
    ASSERT_EQ(1, wire_calls);
    ASSERT_EQ(0x0ABC, wire_ppn);
    ASSERT_EQ((0x0ABCu << 10) + (buf_va & 0x3FF), FLP_DATA.fmt_buf_pa);
    ASSERT_EQ(-1, (int)FLP_DATA.initialized);

    ASSERT_EQ(status_$ok,
              FLP_$DINIT(0, 0, &num_blocks, &sec_per_track, &num_heads,
                         &label, &flags));
    ASSERT_EQ(1, vtop_calls);
    ASSERT_EQ(1, wire_calls);
}

/* 0x00E3E140-0x00E3E14E / 0x00E3E190-0x00E3E1BA: the controller's base,
 * control = 3, cmd_retry = 0, RECALIBRATE of the unit (two words). */
TEST(recalibrate_call)
{
    reset();
    ASSERT_EQ(status_$ok,
              FLP_$DINIT(2, 1, &num_blocks, &sec_per_track, &num_heads,
                         &label, &flags));
    ASSERT_EQ(REGS_VA + 0x40, FLP_DATA.hw_addr);
    ASSERT_EQ(3, ((volatile flp_regs_t *)(va_arena + REGS_VA + 0x40))->control);
    ASSERT_EQ(0, FLP_DATA.cmd_retry);
    ASSERT_EQ(1, excs_calls);
    ASSERT_PTR_EQ(FLP_DATA.recal_cmd, excs_cmd);
    ASSERT_PTR_EQ(&flp_word_two, excs_count);
    ASSERT_EQ(2, excs_cmd_word1);
}

/* 0x00E3E1BE-0x00E3E1D2: the unit's cylinder and disk-change reset, even
 * when the recalibrate failed. */
TEST(unit_state_reset_regardless_of_status)
{
    reset();
    excs_status = status_$disk_not_ready;
    ASSERT_EQ(status_$disk_not_ready,
              FLP_$DINIT(1, 0, &num_blocks, &sec_per_track, &num_heads,
                         &label, &flags));
    ASSERT_EQ(0, FLP_DATA.unit_cyl[1]);
    ASSERT_EQ(0x5555, FLP_DATA.unit_cyl[0]);
    ASSERT_EQ(0, FLP_DATA.disk_change[1]);
    ASSERT_EQ(-1, (int)FLP_DATA.disk_change[2]);
    /* the geometry is NOT filled in ... */
    ASSERT_EQ(0, num_blocks);
    ASSERT_EQ(0xEEEE, sec_per_track);
    /* ... but w_06 is */
    ASSERT_EQ(1, label.w_06);
    ASSERT_EQ(0xEEEEEEEEu, label.l_00);
}

/* 0x00E3E1DC-0x00E3E206: the geometry, from the cells at 0x00E3E21E. */
TEST(geometry_when_no_block_count_given)
{
    reset();
    ASSERT_EQ(status_$ok,
              FLP_$DINIT(0, 0, &num_blocks, &sec_per_track, &num_heads,
                         &label, &flags));
    ASSERT_EQ(0, flags);
    ASSERT_EQ(8, sec_per_track);
    ASSERT_EQ(2, num_heads);
    ASSERT_EQ(0x4D0, num_blocks);
    ASSERT_EQ(0x009204B2u, label.l_00);
    ASSERT_EQ(0, label.w_04);
    ASSERT_EQ(1, label.w_06);
    ASSERT_EQ(0, label.w_08);
}

/* `tst.l (A2)` / `bgt`: a positive block count is left alone; zero or
 * negative is replaced. */
TEST(existing_block_count_is_kept)
{
    reset();
    num_blocks = 5;
    ASSERT_EQ(status_$ok,
              FLP_$DINIT(0, 0, &num_blocks, &sec_per_track, &num_heads,
                         &label, &flags));
    ASSERT_EQ(5, num_blocks);
    ASSERT_EQ(0xEEEE, sec_per_track);
    ASSERT_EQ(1, label.w_06);

    reset();
    num_blocks = -1;
    ASSERT_EQ(status_$ok,
              FLP_$DINIT(0, 0, &num_blocks, &sec_per_track, &num_heads,
                         &label, &flags));
    ASSERT_EQ(0x4D0, num_blocks);
}

/* 0x00E3E19A `clr.w (-0x20,A6)`: the stand-in volume's as_options (the one
 * field EXCS reads, `btst.b #0x1,(0x29,A2)`) is zero on every call. */
TEST(recalibrate_volume_has_zero_as_options)
{
    reset();
    excs_status = status_$ok;
    excs_vol_as_options = 0xFFFF;
    (void)FLP_$DINIT(1, 0, &num_blocks, &sec_per_track, &num_heads, &label, &flags);
    ASSERT_EQ(1, excs_calls);
    ASSERT_EQ(0, excs_vol_as_options);
}

int main(void)
{
    ARCH_HOST_VA_BASE = (uintptr_t)va_arena;

    printf("FLP_$DINIT tests\n");
    RUN_TEST(unit_above_three_is_rejected);
    RUN_TEST(first_call_wires_the_format_buffer);
    RUN_TEST(recalibrate_call);
    RUN_TEST(unit_state_reset_regardless_of_status);
    RUN_TEST(geometry_when_no_block_count_given);
    RUN_TEST(existing_block_count_is_kept);
    RUN_TEST(recalibrate_volume_has_zero_as_options);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
