/*
 * flp/test/test_int.c - FLP_$INT (0x00E19F6C)
 *
 * The FDC is scripted through the FLP_FDC_READ_DATA / FLP_FDC_WRITE_DATA
 * accessors: a read consumes the next byte of a result queue, a write
 * (the SENSE INTERRUPT STATUS command) makes the queue available, and the
 * status byte is recomputed after each access.  RQM is always set - a
 * never-ready FDC would spin the handler for ever, exactly as the image
 * does.  EC_$ADVANCE_WITHOUT_DISPATCH is mocked.
 */

#include <stdio.h>
#include <string.h>

#include "flp/flp.h"

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
 * The scripted FDC
 * ========================================================================== */

static uint8_t va_arena[0x400];
#define REGS_VA 0x100
#define REGS ((volatile flp_regs_t *)(va_arena + REGS_VA))

static uint8_t  fdc_results[8];
static int      fdc_result_count;
static int      fdc_result_index;
static int      fdc_wants_sense;     /* DIO stays clear until a byte is written */
static int      fdc_writes;
static uint8_t  fdc_last_write;

static void fdc_present(void)
{
    if (!fdc_wants_sense && fdc_result_index < fdc_result_count) {
        REGS->status = FLP_STATUS_RQM | FLP_STATUS_DIO;
    } else {
        REGS->status = FLP_STATUS_RQM;
    }
}

static uint8_t fdc_read_data(void)
{
    uint8_t b = 0xFF;
    if (fdc_result_index < fdc_result_count) {
        b = fdc_results[fdc_result_index++];
    }
    fdc_present();
    return b;
}

static void fdc_write_data(uint8_t v)
{
    fdc_writes++;
    fdc_last_write = v;
    fdc_wants_sense = 0;
    fdc_present();
}

#define FLP_FDC_READ_DATA(regs)      ((void)(regs), fdc_read_data())
#define FLP_FDC_WRITE_DATA(regs, v)  ((void)(regs), fdc_write_data(v))

#include "flp/flp_internal.h"

static int                 advance_calls;
static ec_$eventcount_t   *advance_ec;

void EC_$ADVANCE_WITHOUT_DISPATCH(ec_$eventcount_t *ec)
{
    advance_calls++;
    advance_ec = ec;
}

/* ==========================================================================
 * Code under test
 * ========================================================================== */

#include "../flp_data.c"
#include "../int.c"

static dcte_t dcte;

static void reset(void)
{
    memset(va_arena, 0, sizeof(va_arena));
    memset(&dcte, 0, sizeof(dcte));
    dcte.cnum = 1;
    FLP_DATA.ctlr_table[1].hw_addr = REGS_VA;
    FLP_DATA.hw_addr = 0;
    memset(FLP_DATA.sregs, 0xEE, sizeof(FLP_DATA.sregs));
    memset(FLP_DATA.disk_change, 0, sizeof(FLP_DATA.disk_change));

    memset(fdc_results, 0, sizeof(fdc_results));
    fdc_result_count = 0;
    fdc_result_index = 0;
    fdc_wants_sense = 0;
    fdc_writes = 0;
    advance_calls = 0;
    fdc_present();
}

/* ==========================================================================
 * Tests
 * ========================================================================== */

/* The FDC already presents results: three bytes land in FLP_$SREGS as
 * words, the fourth word is left alone, nothing is written, the EC
 * advances, and the result is Domain true. */
TEST(three_result_bytes_are_collected)
{
    reset();
    fdc_results[0] = 0x20; fdc_results[1] = 0x15; fdc_results[2] = 0x07;
    fdc_result_count = 3;
    fdc_present();

    ASSERT_EQ(-1, (int)FLP_$INT(&dcte));
    ASSERT_EQ(REGS_VA, FLP_DATA.hw_addr);       /* 0x00E19F8E */
    ASSERT_EQ(0x0020, FLP_$SREGS[0]);
    ASSERT_EQ(0x0015, FLP_$SREGS[1]);
    ASSERT_EQ(0x0007, FLP_$SREGS[2]);
    ASSERT_EQ(0xEEEE, FLP_$SREGS[3]);
    ASSERT_EQ(0, fdc_writes);
    ASSERT_EQ(1, advance_calls);
    ASSERT_PTR_EQ(&FLP_$EC, advance_ec);
}

/* 0x00E19FB8-0x00E19FBC: bytes beyond the third are read and dropped. */
TEST(extra_result_bytes_are_drained)
{
    reset();
    fdc_results[0] = 0xC0; fdc_results[1] = 0x02; fdc_results[2] = 0x03;
    fdc_results[3] = 0x04; fdc_results[4] = 0x05; fdc_results[5] = 0x06;
    fdc_results[6] = 0x07;
    fdc_result_count = 7;
    fdc_present();

    FLP_$INT(&dcte);
    ASSERT_EQ(7, fdc_result_index);
    ASSERT_EQ(0x00C0, FLP_$SREGS[0]);
    ASSERT_EQ(0x0003, FLP_$SREGS[2]);
    ASSERT_EQ(0xEEEE, FLP_$SREGS[3]);
}

/* 0x00E19FCA-0x00E19FCE: DIO clear with nothing read yet -> the byte 8 is
 * written, then the two SENSE INTERRUPT STATUS result bytes are read. */
TEST(sense_interrupt_status_is_issued_first)
{
    reset();
    fdc_wants_sense = 1;
    fdc_results[0] = 0x20; fdc_results[1] = 0x00;
    fdc_result_count = 2;
    fdc_present();

    FLP_$INT(&dcte);
    ASSERT_EQ(1, fdc_writes);
    ASSERT_EQ(8, fdc_last_write);
    ASSERT_EQ(0x0020, FLP_$SREGS[0]);
    ASSERT_EQ(0x0000, FLP_$SREGS[1]);
    ASSERT_EQ(0xEEEE, FLP_$SREGS[2]);
    ASSERT_EQ(1, advance_calls);
}

/* 0x00E19FDC-0x00E19FF8: ST0's low three bits == 6 flag a disk change on
 * the unit in its low two bits. */
TEST(interrupt_code_6_flags_a_disk_change)
{
    reset();
    fdc_results[0] = 0xC6; fdc_results[1] = 0x00;    /* code 11, unit 2 */
    fdc_result_count = 2;
    fdc_present();
    FLP_$INT(&dcte);
    ASSERT_EQ(0, FLP_DATA.disk_change[0]);
    ASSERT_EQ(-1, (int)FLP_DATA.disk_change[2]);

    reset();
    fdc_results[0] = 0x47; fdc_results[1] = 0x00;    /* code 7: not 6 */
    fdc_result_count = 2;
    fdc_present();
    FLP_$INT(&dcte);
    ASSERT_EQ(0, FLP_DATA.disk_change[3]);
}

int main(void)
{
    ARCH_HOST_VA_BASE = (uintptr_t)va_arena;

    printf("FLP_$INT tests\n");
    RUN_TEST(three_result_bytes_are_collected);
    RUN_TEST(extra_result_bytes_are_drained);
    RUN_TEST(sense_interrupt_status_is_issued_first);
    RUN_TEST(interrupt_code_6_flags_a_disk_change);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
