/*
 * win/test/test_int.c - WIN_$INT (0x00E19BFA)
 *
 * WIN_$CHECK_DISK_STATUS, DMA_$CHECK, SEEK, read_or_write_disk_record and
 * EC_$ADVANCE_WITHOUT_DISPATCH are mocked; the module block is WIN_$DATA
 * and the request chain lives in a VA arena so the +0x60 / +0x5C cells can
 * hold 32-bit addresses.
 */

#include <stdio.h>
#include <string.h>

int __host_intr_disable_count = 0;

#include "win/win_internal.h"

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
    long long _e = (long long)(expected); \
    long long _a = (long long)(actual); \
    if (_e != _a) { \
        printf("FAILED\n    Expected: %lld, Got: %lld at line %d\n", \
               _e, _a, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

MODULE_DATA_DEFINE(win_$data_t, WIN_$DATA, 0x00E2B89C);
uint32_t win_$host_clockh(void) { return 0; }

static uint8_t va_arena[0x400];
#define REQ_A_OFF   0x100
#define REQ_B_OFF   0x180
#define VOL_OFF     0x200
#define REGS_OFF    0x300
#define REQ_A ((win_$request_t *)(va_arena + REQ_A_OFF))
#define REQ_B ((win_$request_t *)(va_arena + REQ_B_OFF))
#define VOL   ((disk_$volume_t *)(va_arena + VOL_OFF))
#define REGS  (va_arena + REGS_OFF)
#define WIN_STATUS (*(status_$t *)(WIN_$DATA.bytes + WIN_STATUS_OFFSET))

static status_$t check_status;
static int check_calls;
status_$t WIN_$CHECK_DISK_STATUS(uint16_t unit) { (void)unit; check_calls++; return check_status; }

static status_$t dma_status;
static int dma_calls;
status_$t DMA_$CHECK(uint16_t channel) { dma_calls++; return channel == 3 ? dma_status : 0x0BAD; }

static status_$t seek_status;
static int seek_calls;
static uint16_t seek_unit, seek_dev_unit;
static void *seek_req;
static uint8_t seek_flags;
status_$t SEEK(uint16_t unit, uint16_t dev_unit, void *req, uint8_t flags)
{
    seek_calls++;
    seek_unit = unit; seek_dev_unit = dev_unit; seek_req = req; seek_flags = flags;
    return seek_status;
}

static status_$t rw_status;
static int rw_calls;
status_$t read_or_write_disk_record(uint16_t unit) { (void)unit; rw_calls++; return rw_status; }

static int advance_calls;
static ec_$eventcount_t *advance_ec;
void EC_$ADVANCE_WITHOUT_DISPATCH(ec_$eventcount_t *ec) { advance_calls++; advance_ec = ec; }

#include "../int.c"

static dcte_t dcte;

static void reset(void)
{
    memset(WIN_$DATA.bytes, 0, sizeof(WIN_$DATA.bytes));
    memset(va_arena, 0, sizeof(va_arena));
    memset(&dcte, 0, sizeof(dcte));
    dcte.cnum = 1;
    *(volatile uint8_t **)(WIN_UNIT(1) + WIN_BASE_ADDR_OFFSET) = REGS;
    REGS[WIN_REG_MODE] = 0x0A;
    VOL->dev_unit = 7;
    REQ_A->next = REQ_B_OFF;
    REQ_A->cylinder = 0x123;
    REQ_B->next = 0;
    REQ_B->cylinder = 0x456;
    WIN_CUR_REQ_VA = REQ_A_OFF;
    WIN_DEV_INFO_VA = VOL_OFF;
    WIN_STATUS = 0x77777777;
    check_status = status_$ok; check_calls = 0;
    dma_status = status_$ok; dma_calls = 0;
    seek_status = status_$ok; seek_calls = 0;
    rw_status = status_$ok; rw_calls = 0;
    advance_calls = 0;
    __host_intr_disable_count = 0;
}

/* 0x00E19C26-0x00E19C30: no chain -> mode idled, eventcount advanced, and
 * nothing else touched (the status word keeps its old value). */
TEST(no_chain_just_wakes)
{
    reset();
    WIN_CUR_REQ_VA = 0;
    ASSERT_EQ(-1, WIN_$INT(&dcte));
    ASSERT_EQ(0, REGS[WIN_REG_MODE]);
    ASSERT_EQ(0, check_calls);
    ASSERT_EQ(1, advance_calls);
    ASSERT_EQ((long long)(intptr_t)WIN_UNIT_EC(1), (long long)(intptr_t)advance_ec);
    ASSERT_EQ(0x77777777, WIN_STATUS);
    ASSERT_EQ(0, __host_intr_disable_count);   /* no SET_SR */
}

/* Seek-completed path (flag set): flag cleared, cylinder noted, transfer
 * started and its status posted; nothing to wake yet. */
TEST(seek_done_starts_transfer)
{
    reset();
    WIN_$DATA.bytes[WIN_FLAG_OFFSET] = 0xFF;
    rw_status = status_$ok;
    ASSERT_EQ(-1, WIN_$INT(&dcte));
    ASSERT_EQ(1, __host_intr_disable_count);   /* move #0x2500,SR */
    ASSERT_EQ(0, WIN_$DATA.bytes[WIN_FLAG_OFFSET]);
    ASSERT_EQ(0x123, *(uint16_t *)(WIN_$DATA.bytes + WIN_CUR_CYL_OFFSET));
    ASSERT_EQ(0, dma_calls);
    ASSERT_EQ(0, seek_calls);
    ASSERT_EQ(1, rw_calls);
    ASSERT_EQ(status_$ok, WIN_STATUS);
    ASSERT_EQ(0, advance_calls);
    ASSERT_EQ(REQ_A_OFF, WIN_CUR_REQ_VA);       /* still on A */
}

/* The transfer's status is posted UNCONDITIONALLY (0x00E19CBE `bra`), and
 * a non-zero one wakes the waiter. */
TEST(transfer_failure_is_posted_and_wakes)
{
    reset();
    WIN_$DATA.bytes[WIN_FLAG_OFFSET] = 0xFF;
    rw_status = (status_$t)-5;                  /* negative: still posted */
    WIN_$INT(&dcte);
    ASSERT_EQ((status_$t)-5, WIN_STATUS);
    ASSERT_EQ(1, advance_calls);
}

/* Seek completed with an error from the status check: no transfer. */
TEST(seek_done_with_error)
{
    reset();
    WIN_$DATA.bytes[WIN_FLAG_OFFSET] = 0xFF;
    check_status = status_$disk_not_ready;
    WIN_$INT(&dcte);
    ASSERT_EQ(0, WIN_$DATA.bytes[WIN_FLAG_OFFSET]);
    ASSERT_EQ(0, rw_calls);
    ASSERT_EQ(status_$disk_not_ready, WIN_STATUS);
    ASSERT_EQ(1, advance_calls);
}

/* Transfer completed: DMA_$CHECK(3) stands in for a zero status, then the
 * chain steps to B and SEEK(unit, vol->dev_unit, B, 0) is issued; a
 * negative result (seek in progress) leaves things pending. */
TEST(transfer_done_steps_to_next_and_seeks)
{
    reset();
    seek_status = (status_$t)-1;
    WIN_$INT(&dcte);
    ASSERT_EQ(1, dma_calls);
    ASSERT_EQ(REQ_B_OFF, WIN_CUR_REQ_VA);
    ASSERT_EQ(1, seek_calls);
    ASSERT_EQ(1, seek_unit);
    ASSERT_EQ(7, seek_dev_unit);
    ASSERT_EQ((long long)(intptr_t)REQ_B, (long long)(intptr_t)seek_req);
    ASSERT_EQ(0, seek_flags);
    ASSERT_EQ(0, rw_calls);
    ASSERT_EQ(status_$ok, WIN_STATUS);          /* negative NOT posted */
    ASSERT_EQ(0, advance_calls);
}

/* A zero SEEK result (already there) transfers at once; a positive one is
 * posted and wakes. */
TEST(seek_zero_transfers_positive_posts)
{
    reset();
    seek_status = status_$ok;
    WIN_$INT(&dcte);
    ASSERT_EQ(1, rw_calls);
    ASSERT_EQ(0, advance_calls);

    reset();
    seek_status = status_$disk_seek_error;
    WIN_$INT(&dcte);
    ASSERT_EQ(0, rw_calls);
    ASSERT_EQ(status_$disk_seek_error, WIN_STATUS);
    ASSERT_EQ(1, advance_calls);
}

/* DMA_$CHECK's status is used only when the check status was zero. */
TEST(dma_status_only_replaces_zero)
{
    reset();
    dma_status = 0x0008001D /* dma not at end of range */;
    WIN_$INT(&dcte);
    ASSERT_EQ(0x0008001D /* dma not at end of range */, WIN_STATUS);
    ASSERT_EQ(0, seek_calls);
    ASSERT_EQ(1, advance_calls);

    reset();
    check_status = status_$DMA_overrun;
    dma_status = 0x0008001D /* dma not at end of range */;
    WIN_$INT(&dcte);
    ASSERT_EQ(status_$DMA_overrun, WIN_STATUS);
}

/* The last request of the chain completed: cell goes NULL, waiter woken. */
TEST(chain_exhausted_wakes)
{
    reset();
    WIN_CUR_REQ_VA = REQ_B_OFF;
    WIN_$INT(&dcte);
    ASSERT_EQ(0, WIN_CUR_REQ_VA);
    ASSERT_EQ(0, seek_calls);
    ASSERT_EQ(status_$ok, WIN_STATUS);
    ASSERT_EQ(1, advance_calls);
}

int main(void)
{
    ARCH_HOST_VA_BASE = (uintptr_t)va_arena;
    printf("WIN_$INT tests\n");
    RUN_TEST(no_chain_just_wakes);
    RUN_TEST(seek_done_starts_transfer);
    RUN_TEST(transfer_failure_is_posted_and_wakes);
    RUN_TEST(seek_done_with_error);
    RUN_TEST(transfer_done_steps_to_next_and_seeks);
    RUN_TEST(seek_zero_transfers_positive_posts);
    RUN_TEST(dma_status_only_replaces_zero);
    RUN_TEST(chain_exhausted_wakes);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
