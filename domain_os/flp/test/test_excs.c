/*
 * flp/test/test_excs.c - EXCS (0x00E3E268)
 *
 * SHAKE, EC_$WAIT, DMA_$CHECK and PARITY_$CHK_IO are mocked; the FDC
 * result registers are set directly in FLP_$SREGS (they are what FLP_$INT
 * would have collected) and the SENSE DRIVE STATUS byte comes back through
 * the SHAKE mock.  Every status branch of the interpreter is walked, plus
 * the retry tail and the recursive RECALIBRATE.
 */

#include <stdio.h>
#include <string.h>

#include "flp/flp_internal.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  Running %-48s ", #name); \
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
 * Globals and mocks
 * ========================================================================== */

ec_$eventcount_t TIME_$CLOCKH_EC = { .value = (int32_t)(0) };  /* TIME_$CLOCKH = its value */

static uint8_t va_arena[0x400];
#define REGS_VA 0x100
#define REGS ((volatile flp_regs_t *)(va_arena + REGS_VA))

/* SHAKE: a log of calls; the n-th call returns shake_status[n] and, for a
 * read of one word, stores shake_read_word. */
static int        shake_calls;
static uint16_t  *shake_data[8];
static int16_t    shake_count[8];
static int16_t    shake_dir[8];
static status_$t  shake_status[8];
static uint16_t   shake_read_word;

status_$t SHAKE(uint16_t *data, int16_t *count_ptr, int16_t *dir_ptr)
{
    int n = shake_calls++;
    if (n < 8) {
        shake_data[n] = data;
        shake_count[n] = *count_ptr;
        shake_dir[n] = *dir_ptr;
        if (*dir_ptr == 0 && *count_ptr == 1) {
            *data = shake_read_word;
        }
        return shake_status[n];
    }
    return status_$ok;
}

static int              wait_calls;
static ec_$wait_ecs_t   wait_ecs;
static ec_$wait_vals_t  wait_vals;
static int16_t          wait_result;

int16_t EC_$WAIT(ec_$wait_ecs_t ecs, ec_$wait_vals_t vals)
{
    wait_calls++;
    wait_ecs = ecs;
    wait_vals = vals;
    return wait_result;
}

static int        dma_check_calls;
static uint16_t   dma_check_channel;
static status_$t  dma_check_status;

status_$t DMA_$CHECK(uint16_t channel)
{
    dma_check_calls++;
    dma_check_channel = channel;
    return dma_check_status;
}

static int       parity_calls;
static uint32_t  parity_arg1, parity_arg2;
static uint32_t  parity_result;

uint32_t PARITY_$CHK_IO(uint32_t ppn1, uint32_t ppn2)
{
    parity_calls++;
    parity_arg1 = ppn1;
    parity_arg2 = ppn2;
    return parity_result;
}

/* ==========================================================================
 * Code under test
 * ========================================================================== */

#include "../flp_data.c"
#include "../excs.c"

static uint16_t cmd[9];
static disk_$volume_t vol;

static void reset(void)
{
    memset(va_arena, 0, sizeof(va_arena));
    FLP_DATA.hw_addr = REGS_VA;
    FLP_DATA.ec.value = 41;
    FLP_DATA.buf_pa = 0x1234;
    FLP_DATA.cmd_retry = 0;
    FLP_DATA.dma_retry = 0;
    memset(FLP_DATA.sregs, 0, sizeof(FLP_DATA.sregs));
    memset(FLP_DATA.unit_cyl, 0x55, sizeof(FLP_DATA.unit_cyl));
    FLP_DATA.recal_cmd[0] = 0x0007;
    FLP_DATA.recal_cmd[1] = 0x7777;
    FLP_DATA.sense_cmd[1] = 0x7777;
    TIME_$CLOCKH = 1000;

    shake_calls = 0;
    memset(shake_status, 0, sizeof(shake_status));
    shake_read_word = 0;
    wait_calls = 0;
    wait_result = 0;
    dma_check_calls = 0;
    dma_check_status = status_$ok;
    parity_calls = 0;
    parity_result = 0;

    memset(cmd, 0, sizeof(cmd));
    cmd[0] = 0x0046;                /* READ DATA */
    cmd[1] = 0x0001;                /* unit 1, head 0 */
    memset(&vol, 0, sizeof(vol));
}

/* ==========================================================================
 * Tests
 * ========================================================================== */

/* 0x00E3E288-0x00E3E2CA: the command goes out written; the wait is on
 * FLP_$EC + 1 and the clock + 8. */
TEST(command_is_written_then_waited_for)
{
    reset();
    ASSERT_EQ(status_$ok, EXCS(cmd, &flp_word_nine, &vol));

    ASSERT_EQ(1, shake_calls);
    ASSERT_PTR_EQ(cmd, shake_data[0]);
    ASSERT_EQ(9, shake_count[0]);
    ASSERT_EQ(1, shake_dir[0]);
    ASSERT_EQ(1, wait_calls);
    ASSERT_PTR_EQ(&FLP_$EC, wait_ecs.ec[0]);
    ASSERT_PTR_EQ(&TIME_$CLOCKH, wait_ecs.ec[1]);
    ASSERT_PTR_EQ(NULL, wait_ecs.ec[2]);
    ASSERT_EQ(42, wait_vals.val[0]);
    ASSERT_EQ(1008, wait_vals.val[1]);
    ASSERT_EQ(0, wait_vals.val[2]);
    ASSERT_EQ(1, dma_check_calls);
    ASSERT_EQ(3, dma_check_channel);
}

/* 0x00E3E29A-0x00E3E29C / 0x00E3E32A: a handshake failure is returned. */
TEST(handshake_failure_is_returned)
{
    reset();
    shake_status[0] = status_$disk_controller_timeout;
    ASSERT_EQ(status_$disk_controller_timeout, EXCS(cmd, &flp_word_nine, &vol));
    ASSERT_EQ(0, wait_calls);
}

/* 0x00E3E2CC-0x00E3E2D6: command code 7 skips the DMA and parity checks. */
TEST(recalibrate_skips_the_dma_checks)
{
    reset();
    cmd[0] = 0x0007;
    REGS->w_06 = 0x0002;
    dma_check_status = 0x00080004;
    ASSERT_EQ(status_$ok, EXCS(cmd, &flp_word_two, &vol));
    ASSERT_EQ(0, dma_check_calls);
    ASSERT_EQ(0, parity_calls);
}

/* 0x00E3E2D8-0x00E3E304: register word +6 bit 1 -> PARITY_$CHK_IO(1, page);
 * a non-zero low word is fatal. */
TEST(parity_error_during_write)
{
    reset();
    REGS->w_06 = 0x0002;
    parity_result = 0x00010000;         /* high word only: ignored */
    ASSERT_EQ(status_$ok, EXCS(cmd, &flp_word_nine, &vol));
    ASSERT_EQ(1, parity_calls);
    ASSERT_EQ(1, parity_arg1);
    ASSERT_EQ(0x1234, parity_arg2);

    reset();
    REGS->w_06 = 0x0002;
    parity_result = 0x0001;
    FLP_DATA.cmd_retry = 5;
    ASSERT_EQ(status_$memory_parity_error_during_disk_write,
              EXCS(cmd, &flp_word_nine, &vol));
    ASSERT_EQ(5, FLP_DATA.cmd_retry);   /* returned directly, no retry */
}

/* 0x00E3E308-0x00E3E32C: DMA_$CHECK errors; "not at end of range" is fine,
 * others retry while cmd_retry lasts. */
TEST(dma_check_outcomes)
{
    reset();
    dma_check_status = status_$dma_not_at_end_of_range;
    ASSERT_EQ(status_$ok, EXCS(cmd, &flp_word_nine, &vol));

    reset();
    dma_check_status = 0x00080004;
    ASSERT_EQ(0x00080004, EXCS(cmd, &flp_word_nine, &vol));

    reset();
    dma_check_status = 0x00080004;
    FLP_DATA.cmd_retry = 2;
    ASSERT_EQ(FLP_$RETRY, EXCS(cmd, &flp_word_nine, &vol));
    ASSERT_EQ(1, FLP_DATA.cmd_retry);
}

/* 0x00E3E330-0x00E3E334 then 0x00E3E386: a clock wake-up forges ST0 = 0x10,
 * which after the sense reads as equipment check. */
TEST(timeout_is_reported_as_equipment_check)
{
    reset();
    wait_result = 1;
    ASSERT_EQ(status_$disk_equipment_check, EXCS(cmd, &flp_word_nine, &vol));
    ASSERT_EQ(0x10, FLP_$SREGS[0]);
    /* the sense: two words written from sense_cmd (unit/head copied from
     * the command), one word read into the local */
    ASSERT_EQ(3, shake_calls);
    ASSERT_PTR_EQ(FLP_DATA.sense_cmd, shake_data[1]);
    ASSERT_EQ(2, shake_count[1]);
    ASSERT_EQ(1, shake_dir[1]);
    ASSERT_EQ(1, FLP_DATA.sense_cmd[1]);
    ASSERT_EQ(1, shake_count[2]);
    ASSERT_EQ(0, shake_dir[2]);
}

/* 0x00E3E33A-0x00E3E342: ST0 bits outside 0xD8 do not matter. */
TEST(uninteresting_st0_is_success)
{
    reset();
    FLP_$SREGS[0] = 0x0027;
    ASSERT_EQ(status_$ok, EXCS(cmd, &flp_word_nine, &vol));
    ASSERT_EQ(1, shake_calls);          /* no sense */
}

/* 0x00E3E360-0x00E3E382: a failing sense handshake is the result. */
TEST(sense_handshake_failure_is_returned)
{
    reset();
    FLP_$SREGS[0] = 0x0040;
    shake_status[1] = status_$disk_controller_timeout;
    ASSERT_EQ(status_$disk_controller_timeout, EXCS(cmd, &flp_word_nine, &vol));

    reset();
    FLP_$SREGS[0] = 0x0040;
    shake_status[2] = status_$disk_controller_error;
    ASSERT_EQ(status_$disk_controller_error, EXCS(cmd, &flp_word_nine, &vol));
}

/* 0x00E3E398-0x00E3E3C8: ST0 bit 3. */
TEST(not_ready_and_the_two_sided_check)
{
    reset();
    FLP_$SREGS[0] = 0x0008;
    FLP_DATA.cmd_retry = 3;
    shake_read_word = 0x0020;           /* ST3: ready, two-sided */
    cmd[1] = 0x0003;                    /* head 0 */
    ASSERT_EQ(status_$disk_not_ready, EXCS(cmd, &flp_word_nine, &vol));
    ASSERT_EQ(0, FLP_DATA.cmd_retry);   /* the budget is dropped */

    reset();
    FLP_$SREGS[0] = 0x0008;
    shake_read_word = 0x0020;
    cmd[1] = 0x0005;                    /* head 1, unit 1 */
    ASSERT_EQ(status_$floppy_is_not_2_sided, EXCS(cmd, &flp_word_nine, &vol));

    reset();
    FLP_$SREGS[0] = 0x0008;
    shake_read_word = 0x0028;           /* ST3 bit 3 set */
    cmd[1] = 0x0005;
    ASSERT_EQ(status_$disk_not_ready, EXCS(cmd, &flp_word_nine, &vol));

    reset();
    FLP_$SREGS[0] = 0x0008;
    shake_read_word = 0x0000;           /* ST3 bit 5 clear */
    cmd[1] = 0x0005;
    ASSERT_EQ(status_$disk_not_ready, EXCS(cmd, &flp_word_nine, &vol));
}

/* 0x00E3E3CC-0x00E3E3DE: interrupt code 11 (ST0 bits 7:6). */
TEST(interrupt_code_three_is_not_ready)
{
    reset();
    FLP_$SREGS[0] = 0x00C0;
    FLP_DATA.cmd_retry = 4;
    ASSERT_EQ(status_$disk_not_ready, EXCS(cmd, &flp_word_nine, &vol));
    ASSERT_EQ(0, FLP_DATA.cmd_retry);   /* 0x00E3E442 */
}

/* 0x00E3E3E0-0x00E3E3EE: ST1 bit 1. */
TEST(write_protected)
{
    reset();
    FLP_$SREGS[0] = 0x0040;
    FLP_$SREGS[1] = 0x0002;
    FLP_DATA.cmd_retry = 4;
    ASSERT_EQ(status_$disk_write_protected, EXCS(cmd, &flp_word_nine, &vol));
    ASSERT_EQ(0, FLP_DATA.cmd_retry);
}

/* 0x00E3E3F0-0x00E3E446: ST1 & 0x85 -> bad format, with the recalibrate. */
TEST(bad_format_paths)
{
    /* no wrong-cylinder bit: through the tail, no retries left */
    reset();
    FLP_$SREGS[0] = 0x0040;
    FLP_$SREGS[1] = 0x0004;
    ASSERT_EQ(status_$bad_disk_format, EXCS(cmd, &flp_word_nine, &vol));

    /* no wrong-cylinder bit, retries left: retry */
    reset();
    FLP_$SREGS[0] = 0x0040;
    FLP_$SREGS[1] = 0x0080;
    FLP_DATA.cmd_retry = 3;
    ASSERT_EQ(FLP_$RETRY, EXCS(cmd, &flp_word_nine, &vol));
    ASSERT_EQ(2, FLP_DATA.cmd_retry);

    /* wrong cylinder but no retries: bad format */
    reset();
    FLP_$SREGS[0] = 0x0040;
    FLP_$SREGS[1] = 0x0001;
    FLP_$SREGS[2] = 0x0010;
    ASSERT_EQ(status_$bad_disk_format, EXCS(cmd, &flp_word_nine, &vol));
    ASSERT_EQ(3, shake_calls);          /* no recalibrate */
}

/* The recursive RECALIBRATE: cmd_retry is set to 1 first, the unit is the
 * command's low two bits, its cylinder is forgotten, and the inner call's
 * result decides.  The inner EXCS sees ST0 = 0 (the mock leaves the
 * registers as they are) - but its SHAKE is the 4th call, so give the
 * inner command a clean ST0 by clearing sregs from the SHAKE mock's
 * perspective: here the outer registers stay set, so the inner run walks
 * the same bad-format branch with cmd_retry == 1 -> it recalibrates once
 * more (cmd_retry stays 1) and so on; to keep it finite the inner SHAKE
 * (call index 3) is made to fail. */
TEST(bad_format_with_wrong_cylinder_recalibrates)
{
    reset();
    FLP_$SREGS[0] = 0x0040;
    FLP_$SREGS[1] = 0x0001;
    FLP_$SREGS[2] = 0x0010;
    FLP_DATA.cmd_retry = 5;
    cmd[1] = 0x0006;                    /* unit 2, head 1 */
    shake_status[3] = status_$disk_controller_timeout;   /* the inner write */

    ASSERT_EQ(status_$disk_controller_timeout, EXCS(cmd, &flp_word_nine, &vol));
    ASSERT_EQ(4, shake_calls);
    ASSERT_PTR_EQ(FLP_DATA.recal_cmd, shake_data[3]);
    ASSERT_EQ(2, shake_count[3]);
    ASSERT_EQ(2, FLP_DATA.recal_cmd[1]);
    ASSERT_EQ(0, FLP_DATA.unit_cyl[2]);
    ASSERT_EQ(0x5555, FLP_DATA.unit_cyl[1]);
    ASSERT_EQ(0, FLP_DATA.cmd_retry);   /* 0x00E3E442 after the failure */
}

/* 0x00E3E448-0x00E3E45E: data check, with and without the volume option. */
TEST(data_check)
{
    reset();
    FLP_$SREGS[0] = 0x0040;
    FLP_$SREGS[1] = 0x0020;
    FLP_DATA.cmd_retry = 2;
    ASSERT_EQ(FLP_$RETRY, EXCS(cmd, &flp_word_nine, &vol));
    ASSERT_EQ(1, FLP_DATA.cmd_retry);

    reset();
    FLP_$SREGS[0] = 0x0040;
    FLP_$SREGS[1] = 0x0020;
    FLP_DATA.cmd_retry = 2;
    vol.as_options = 0x0002;            /* bit 1 of the byte at +0x29 */
    ASSERT_EQ(status_$disk_data_check, EXCS(cmd, &flp_word_nine, &vol));
    ASSERT_EQ(2, FLP_DATA.cmd_retry);   /* returned directly */
}

/* 0x00E3E460-0x00E3E47A: overrun uses the DMA budget, not cmd_retry. */
TEST(overrun)
{
    reset();
    FLP_$SREGS[0] = 0x0040;
    FLP_$SREGS[1] = 0x0010;
    FLP_DATA.dma_retry = 2;
    FLP_DATA.cmd_retry = 7;
    ASSERT_EQ(FLP_$RETRY, EXCS(cmd, &flp_word_nine, &vol));
    ASSERT_EQ(1, FLP_DATA.dma_retry);
    ASSERT_EQ(7, FLP_DATA.cmd_retry);   /* 0x00E3E48C skips the decrement */

    reset();
    FLP_$SREGS[0] = 0x0040;
    FLP_$SREGS[1] = 0x0010;
    FLP_DATA.dma_retry = 0;
    FLP_DATA.cmd_retry = 7;
    ASSERT_EQ(status_$DMA_overrun, EXCS(cmd, &flp_word_nine, &vol));
    ASSERT_EQ(7, FLP_DATA.cmd_retry);
}

/* 0x00E3E47C: nothing recognisable in ST1. */
TEST(unknown_status)
{
    reset();
    FLP_$SREGS[0] = 0x0040;
    FLP_$SREGS[1] = 0x0040;
    ASSERT_EQ(status_$unknown_status_returned_by_hardware,
              EXCS(cmd, &flp_word_nine, &vol));

    reset();
    FLP_$SREGS[0] = 0x0040;
    FLP_$SREGS[1] = 0x0040;
    FLP_DATA.cmd_retry = 1;
    ASSERT_EQ(FLP_$RETRY, EXCS(cmd, &flp_word_nine, &vol));
    ASSERT_EQ(0, FLP_DATA.cmd_retry);
}

int main(void)
{
    ARCH_HOST_VA_BASE = (uintptr_t)va_arena;

    printf("EXCS tests\n");
    RUN_TEST(command_is_written_then_waited_for);
    RUN_TEST(handshake_failure_is_returned);
    RUN_TEST(recalibrate_skips_the_dma_checks);
    RUN_TEST(parity_error_during_write);
    RUN_TEST(dma_check_outcomes);
    RUN_TEST(timeout_is_reported_as_equipment_check);
    RUN_TEST(uninteresting_st0_is_success);
    RUN_TEST(sense_handshake_failure_is_returned);
    RUN_TEST(not_ready_and_the_two_sided_check);
    RUN_TEST(interrupt_code_three_is_not_ready);
    RUN_TEST(write_protected);
    RUN_TEST(bad_format_paths);
    RUN_TEST(bad_format_with_wrong_cylinder_recalibrates);
    RUN_TEST(data_check);
    RUN_TEST(overrun);
    RUN_TEST(unknown_status);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
