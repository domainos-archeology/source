/*
 * flp/test/test_flp_do_io.c - FLP_DO_IO (0x00E3DDC6), FLP_$DO_IO
 * (0x00E3DFE2) and FLP_FORMAT_TRACK (0x00E3DC78)
 *
 * EXCS, ML_$LOCK / ML_$UNLOCK are mocked; the FDC registers, the volume's
 * device entry and the DCTE live in a VA arena the host ARCH_VA_TO_PTR maps;
 * the DISK per-process slots are in the test's DISK_$DATA; the DMAC
 * channel-3 registers are the flp_$dmac_cells array, which the test makes
 * the SAU2 DMAC (SAU2_DMAC_BASE, arch/m68k/sau2/hw.h) for the host.
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
 * The VA arena the FDC registers, device entry and DCTE are reached through
 * (their records hold 32-bit VAs).
 * ========================================================================== */

#define ARENA_SIZE      0x1000
#define ARENA_VA_BASE   0x00E7A000u         /* arena[0] is this VA */
static uint8_t va_arena[ARENA_SIZE];

#define VA_OF(off)      (ARENA_VA_BASE + (off))
#define REGS_OFF        0x100
#define DCTE_OFF        0x200
#define DEV_OFF         0x300
#define REGS ((volatile flp_regs_t *)(va_arena + REGS_OFF))
#define DCTE ((dcte_t *)(va_arena + DCTE_OFF))
#define DEV  ((disk_device_entry_t *)(va_arena + DEV_OFF))
_Alignas(16) uint8_t DISK_$DATA[DISK_$DATA_SIZE];
#define PER_PROC(pid) (DISK_$PER_PROC[pid])

/* The DMAC register window (0xFFA000 on the SAU2). */
static volatile uint8_t flp_$dmac_cells[0x100];
#define SAU2_DMAC_BASE ((uintptr_t)flp_$dmac_cells)

/* ==========================================================================
 * Mocks
 * ========================================================================== */

static int       lock_calls, unlock_calls;
static int16_t   lock_id, unlock_id;

void ML_$LOCK(int16_t resource_id)   { lock_calls++;   lock_id = resource_id; }
void ML_$UNLOCK(int16_t resource_id) { unlock_calls++; unlock_id = resource_id; }

static int        excs_calls;
static uint16_t  *excs_cmd[8];
static uint16_t   excs_cmd_words[8][9];
static int16_t   *excs_count[8];
static status_$t  excs_status[8];
static uint16_t   excs_sregs1;          /* what "the FDC" reports as PCN */
static uint8_t    excs_control_seen[8]; /* the board control byte at call time */
static uint8_t    excs_ocr_seen[8];
static uint16_t   excs_mtc_seen[8];
static uint32_t   excs_mar_seen[8];

status_$t EXCS(uint16_t *cmd, int16_t *count_ptr, disk_$volume_t *vol)
{
    int n = excs_calls++;
    (void)vol;
    if (n < 8) {
        excs_cmd[n] = cmd;
        memcpy(excs_cmd_words[n], cmd, sizeof(excs_cmd_words[n]));
        excs_count[n] = count_ptr;
        excs_control_seen[n] = REGS->control;
        excs_ocr_seen[n] = FLP_DMAC_OCR;
        excs_mtc_seen[n] = FLP_DMAC_MTC;
        excs_mar_seen[n] = FLP_DMAC_MAR;
        FLP_$SREGS[1] = excs_sregs1;
        return excs_status[n];
    }
    return status_$ok;
}

/* ==========================================================================
 * Code under test
 * ========================================================================== */

#include "../flp_data.c"
#include "../format_track.c"
#include "../flp_do_io.c"
#include "../do_io.c"

static disk_$volume_t vol;
static disk_io_req_t req;
static int8_t result;

static void reset(void)
{
    memset(va_arena, 0, sizeof(va_arena));
    memset(DISK_$DATA, 0, sizeof(DISK_$DATA));
    memset((void *)flp_$dmac_cells, 0, sizeof(flp_$dmac_cells));

    DEV->controller = 1;
    DCTE->disk_error_que = 0x00230000u;         /* lock word 0x23 */
    FLP_DATA.ctlr_table[1].dcte_va = VA_OF(DCTE_OFF);
    FLP_DATA.ctlr_table[1].hw_addr = VA_OF(REGS_OFF);
    FLP_DATA.hw_addr = 0;
    FLP_DATA.base_cmd = 0x0040;
    FLP_DATA.fmt_n = 0x0003;
    FLP_DATA.fmt_buf_pa = 0x00ABC080;
    FLP_DATA.cmd_retry = 0x7777;
    FLP_DATA.dma_retry = 0x7777;
    FLP_DATA.buf_pa = 0;
    memset(FLP_DATA.rw_cmd, 0, sizeof(FLP_DATA.rw_cmd));
    /* seek_cmd[0] = 0x0F is initialised data the image never writes */
    FLP_DATA.seek_cmd[1] = FLP_DATA.seek_cmd[2] = 0;
    FLP_DATA.fmt_cmd[1] = 0;
    memset(FLP_DATA.unit_cyl, 0, sizeof(FLP_DATA.unit_cyl));
    memset(FLP_DATA.disk_change, 0, sizeof(FLP_DATA.disk_change));
    memset(FLP_DATA.io_buffer, 0xEE, sizeof(FLP_DATA.io_buffer));
    memset(FLP_DATA.sregs, 0, sizeof(FLP_DATA.sregs));

    memset(&vol, 0, sizeof(vol));
    vol.dev_info = DEV;
    vol.dev_unit = 2;
    vol.sec_per_track = 8;

    memset(&req, 0, sizeof(req));
    req.daddr = 0x00150103u;    /* cylinder 0x15, head 1, sector 3 */
    req.ppn = 0x0ABC;
    req.owner = 5;
    req.op_flags = 0x01;        /* read */
    req.status = 0x77777777;
    PER_PROC(5).io_pending = -1;

    result = 0x5A;
    lock_calls = unlock_calls = 0;
    excs_calls = 0;
    memset(excs_status, 0, sizeof(excs_status));
    excs_sregs1 = 0x0015;
}

/* ==========================================================================
 * Tests
 * ========================================================================== */

/* 0x00E3DDD4-0x00E3DE10 / 0x00E3DFC4-0x00E3DFD0: base from the controller
 * slot, result cleared, the lock word from the DCTE's +0x3C. */
TEST(lock_and_unlock_bracket_the_request)
{
    reset();
    FLP_$DO_IO(&vol, &req, NULL, &result);
    ASSERT_EQ(VA_OF(REGS_OFF), FLP_DATA.hw_addr);
    ASSERT_EQ(0, result);
    ASSERT_EQ(1, lock_calls);
    ASSERT_EQ(0x23, lock_id);
    ASSERT_EQ(1, unlock_calls);
    ASSERT_EQ(0x23, unlock_id);
}

/* A read on the right cylinder: no seek, control 2, DMAC device->memory,
 * READ DATA (0x46), nine words, sector = req + 0 + 1. */
TEST(read_on_current_cylinder)
{
    reset();
    FLP_DATA.unit_cyl[2] = 0x15;
    FLP_$DO_IO(&vol, &req, NULL, &result);

    ASSERT_EQ(1, excs_calls);
    ASSERT_PTR_EQ(FLP_DATA.rw_cmd, excs_cmd[0]);
    ASSERT_PTR_EQ(&flp_word_nine, excs_count[0]);
    ASSERT_EQ(0x0046, excs_cmd_words[0][0]);
    ASSERT_EQ(1 * 4 + 2, excs_cmd_words[0][1]);     /* head*4 + unit */
    ASSERT_EQ(0x0015, excs_cmd_words[0][2]);
    ASSERT_EQ(0x0001, excs_cmd_words[0][3]);
    ASSERT_EQ(0x0004, excs_cmd_words[0][4]);        /* sector 3 + 1 */
    ASSERT_EQ(2, excs_control_seen[0]);
    ASSERT_EQ(0x92, excs_ocr_seen[0]);
    ASSERT_EQ(0x200, excs_mtc_seen[0]);
    ASSERT_EQ(0x0ABCu << 10, excs_mar_seen[0]);
    ASSERT_EQ(1, FLP_DMAC_MFC);
    ASSERT_EQ(0x80, FLP_DMAC_CCR);
    ASSERT_EQ(0x0ABC, FLP_DATA.buf_pa);
    ASSERT_EQ(0x19, FLP_DATA.cmd_retry);
    ASSERT_EQ(0x1F4, FLP_DATA.dma_retry);
    ASSERT_EQ(3, REGS->control);                    /* 0x00E3DF9C */
    ASSERT_EQ(0x77777777, req.status);              /* untouched */
    ASSERT_EQ(-1, (int)PER_PROC(5).io_pending);
}

/* 0x00E3DEA8-0x00E3DED6: a different cylinder seeks first with the first
 * three words of the block (cmd 0x0F), and records what the FDC says. */
TEST(seek_then_write)
{
    reset();
    req.op_flags = 0x02;
    FLP_DATA.unit_cyl[2] = 0x03;
    excs_sregs1 = 0x0015;
    FLP_$DO_IO(&vol, &req, NULL, &result);

    ASSERT_EQ(2, excs_calls);
    ASSERT_PTR_EQ(FLP_DATA.rw_cmd, excs_cmd[0]);
    ASSERT_PTR_EQ(&flp_word_three, excs_count[0]);
    ASSERT_EQ(0x000F, excs_cmd_words[0][0]);
    ASSERT_EQ(0x0015, FLP_DATA.unit_cyl[2]);
    ASSERT_EQ(0x0045, excs_cmd_words[1][0]);        /* WRITE DATA */
    ASSERT_EQ(3, excs_control_seen[1]);
    ASSERT_EQ(0x12, excs_ocr_seen[1]);
}

/* The seek's PCN is recorded even when the seek fails, and the failure
 * retires the request: pending byte cleared, status stored. */
TEST(seek_failure_retires_the_request)
{
    reset();
    FLP_DATA.unit_cyl[2] = 0x03;
    excs_status[0] = status_$disk_not_ready;
    excs_sregs1 = 0x0007;
    FLP_$DO_IO(&vol, &req, NULL, &result);

    ASSERT_EQ(1, excs_calls);
    ASSERT_EQ(0x0007, FLP_DATA.unit_cyl[2]);
    ASSERT_EQ(status_$disk_not_ready, req.status);
    ASSERT_EQ(0, PER_PROC(5).io_pending);
    ASSERT_EQ(3, REGS->control);
    ASSERT_EQ(1, unlock_calls);
}

/* 0x00E3DF68-0x00E3DF7C: FLP_$RETRY loops back to the busy check and
 * re-issues; the sector word does not advance until a transfer is done. */
TEST(retry_marker_repeats_the_transfer)
{
    reset();
    FLP_DATA.unit_cyl[2] = 0x15;
    excs_status[0] = FLP_$RETRY;
    excs_status[1] = status_$ok;
    FLP_$DO_IO(&vol, &req, NULL, &result);

    ASSERT_EQ(2, excs_calls);
    ASSERT_EQ(0x0004, excs_cmd_words[0][4]);
    ASSERT_EQ(0x0004, excs_cmd_words[1][4]);
    ASSERT_EQ(0x77777777, req.status);
}

/* 0x00E3DE64-0x00E3DE7A: a busy FDC fails the request at once. */
TEST(busy_fdc_is_controller_busy)
{
    reset();
    REGS->status = 0x01;
    FLP_$DO_IO(&vol, &req, NULL, &result);

    ASSERT_EQ(0, excs_calls);
    ASSERT_EQ(status_$disk_controller_busy, req.status);
    ASSERT_EQ(0, PER_PROC(5).io_pending);
    ASSERT_EQ(1, unlock_calls);
}

/* 0x00E3DE2C-0x00E3DE46: a write after a disk change; and 0x00E3DF80-
 * 0x00E3DF92: a disk change noticed during the transfer. */
TEST(disk_change_refuses_a_write_and_fails_a_read)
{
    reset();
    req.op_flags = 0x02;
    FLP_DATA.disk_change[2] = -1;
    FLP_$DO_IO(&vol, &req, NULL, &result);
    ASSERT_EQ(0, excs_calls);
    ASSERT_EQ(status_$storage_module_stopped, req.status);
    ASSERT_EQ(0x7777, FLP_DATA.cmd_retry);      /* before the budgets */

    reset();
    FLP_DATA.unit_cyl[2] = 0x15;
    FLP_DATA.disk_change[2] = -1;               /* a read goes ahead ... */
    FLP_$DO_IO(&vol, &req, NULL, &result);
    ASSERT_EQ(1, excs_calls);                   /* ... and then fails */
    ASSERT_EQ(status_$storage_module_stopped, req.status);
}

/* 0x00E3DE12-0x00E3DE28: op 3 goes to FLP_FORMAT_TRACK, then unlock. */
TEST(format_request_builds_the_id_table)
{
    int i;

    reset();
    req.op_flags = 0x03;
    FLP_DATA.unit_cyl[2] = 0x15;
    FLP_$DO_IO(&vol, &req, NULL, &result);

    /* 0x00E3DC9E-0x00E3DCD6: 8 entries of C, H, R, N */
    for (i = 0; i < 8; i++) {
        ASSERT_EQ(0x15, FLP_DATA.io_buffer[4 * i + 0]);
        ASSERT_EQ(0x01, FLP_DATA.io_buffer[4 * i + 1]);
        ASSERT_EQ(i + 1, FLP_DATA.io_buffer[4 * i + 2]);
        ASSERT_EQ(0x03, FLP_DATA.io_buffer[4 * i + 3]);
    }
    ASSERT_EQ(0xEE, FLP_DATA.io_buffer[32]);    /* untouched beyond */
    ASSERT_EQ(0, FLP_DATA.cmd_retry);           /* 0x00E3DCDA */

    /* no seek: one EXCS, FORMAT TRACK with six words */
    ASSERT_EQ(1, excs_calls);
    ASSERT_PTR_EQ(FLP_DATA.fmt_cmd, excs_cmd[0]);
    ASSERT_PTR_EQ(&flp_word_six, excs_count[0]);
    ASSERT_EQ(1 * 4 + 2, FLP_DATA.fmt_cmd[1]);
    ASSERT_EQ(3, excs_control_seen[0]);
    ASSERT_EQ(0x12, excs_ocr_seen[0]);
    ASSERT_EQ((8u << 2) >> 1, excs_mtc_seen[0]);
    ASSERT_EQ(0x00ABC080u, excs_mar_seen[0]);
    ASSERT_EQ(1, FLP_DMAC_MFC);
    ASSERT_EQ(0x80, FLP_DMAC_CCR);
    ASSERT_EQ(1, unlock_calls);
    ASSERT_EQ(0x77777777, req.status);
}

/* FLP_FORMAT_TRACK's seek uses the separate block at +0x118. */
TEST(format_seeks_with_the_seek_block)
{
    reset();
    req.op_flags = 0x03;
    FLP_DATA.unit_cyl[2] = 0x00;
    excs_sregs1 = 0x0015;
    FLP_$DO_IO(&vol, &req, NULL, &result);

    ASSERT_EQ(2, excs_calls);
    ASSERT_PTR_EQ(FLP_DATA.seek_cmd, excs_cmd[0]);
    ASSERT_PTR_EQ(&flp_word_three, excs_count[0]);
    ASSERT_EQ(0x000F, excs_cmd_words[0][0]);
    ASSERT_EQ(1 * 4 + 2, excs_cmd_words[0][1]);
    ASSERT_EQ(0x0015, excs_cmd_words[0][2]);
    ASSERT_EQ(0x0015, FLP_DATA.unit_cyl[2]);
    ASSERT_PTR_EQ(FLP_DATA.fmt_cmd, excs_cmd[1]);
}

/* Format failure paths: busy FDC, and a failing FORMAT TRACK. */
TEST(format_failures_retire_the_request)
{
    reset();
    req.op_flags = 0x03;
    REGS->status = 0x10;
    FLP_$DO_IO(&vol, &req, NULL, &result);
    ASSERT_EQ(0, excs_calls);
    ASSERT_EQ(status_$disk_controller_busy, req.status);
    ASSERT_EQ(0, PER_PROC(5).io_pending);

    reset();
    req.op_flags = 0x03;
    FLP_DATA.unit_cyl[2] = 0x15;
    excs_status[0] = status_$disk_write_protected;
    FLP_$DO_IO(&vol, &req, NULL, &result);
    ASSERT_EQ(status_$disk_write_protected, req.status);
    ASSERT_EQ(0, PER_PROC(5).io_pending);
}

/* 0x00E3DC9E-0x00E3DCA2: no sectors per track -> no table at all. */
TEST(format_with_zero_sectors_builds_nothing)
{
    reset();
    req.op_flags = 0x03;
    vol.sec_per_track = 0;
    FLP_DATA.unit_cyl[2] = 0x15;
    FLP_$DO_IO(&vol, &req, NULL, &result);
    ASSERT_EQ(0xEE, FLP_DATA.io_buffer[0]);
    ASSERT_EQ(0, excs_mtc_seen[0]);
}

int main(void)
{
    ARCH_HOST_VA_BASE = (uintptr_t)va_arena - ARENA_VA_BASE;

    printf("FLP_DO_IO / FLP_FORMAT_TRACK tests\n");
    RUN_TEST(lock_and_unlock_bracket_the_request);
    RUN_TEST(read_on_current_cylinder);
    RUN_TEST(seek_then_write);
    RUN_TEST(seek_failure_retires_the_request);
    RUN_TEST(retry_marker_repeats_the_transfer);
    RUN_TEST(busy_fdc_is_controller_busy);
    RUN_TEST(disk_change_refuses_a_write_and_fails_a_read);
    RUN_TEST(format_request_builds_the_id_table);
    RUN_TEST(format_seeks_with_the_seek_block);
    RUN_TEST(format_failures_retire_the_request);
    RUN_TEST(format_with_zero_sectors_builds_nothing);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
