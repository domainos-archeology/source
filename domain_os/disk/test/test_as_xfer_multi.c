/*
 * disk/test/test_as_xfer_multi.c - Unit tests for DISK_$AS_XFER_MULTI
 * (0x00E6B962)
 *
 * disk/as_xfer_multi.c is #included below with MST_$WIRE, WP_$UNWIRE,
 * CACHE_$FLUSH_VIRTUAL, DISK_$GET_QBLKS, DISK_$RTN_QBLKS, DISK_$READ_MULTI
 * and DISK_$WRITE_MULTI mocked.  The queue blocks live in a host arena that
 * ARCH_HOST_VA_BASE points at, so the VA cells hold arena offsets.
 *
 * The behaviours pinned here are the ones the disassembly settles:
 *   - op_type 1 copies the headers IN and calls WRITE_MULTI
 *     (0x00E6B9CA, 0x00E6BAE6)
 *   - the header copy-OUT happens only for op_type == 0 (`tst.w D4w; bne`
 *     at 0x00E6BB50 and 0x00E6BBB4), not for every non-write op_type
 *   - a misaligned buffer stops the gather (0x00E6B9E2) and every page
 *     from pages_done on is marked "transfer not executed" (0x00E6BBF8)
 *   - a wire failure on page i unwires pages 0..i-1 (0x00E6BA42)
 */

#include <stdio.h>
#include <string.h>

#include "disk/disk_internal.h"
#include "mst/mst.h"
#include "cache/cache.h"
#include "wp/wp.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  Running %s... ", #name); \
    test_##name(); \
    tests_passed++; \
    printf("PASSED\n"); \
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

/* ============================================================================
 * Mocks
 * ============================================================================ */

/* Arena: offset 0 is nil, so the blocks start at 0x40. */
static uint8_t arena[0x40 * 8];
#define BLOCK_VA(n)   ((uint32_t)(0x40 * ((n) + 1)))
#define BLOCK(n)      ((disk_io_req_t *)(arena + BLOCK_VA(n)))

static int wire_calls;
static uint32_t wire_vas[16];
static status_$t wire_fail_status;
static int wire_fail_at;            /* 0-based page index, or -1 */

static int unwire_calls;
static uint32_t unwired[16];

static int flush_calls;

static int get_qblks_calls;
static int16_t get_qblks_count;
static int rtn_qblks_calls;
static int16_t rtn_qblks_count;

static int read_multi_calls;
static int write_multi_calls;
static int16_t read_multi_pages_done;
static status_$t read_multi_status;
static status_$t write_multi_status;
static status_$t block_status[16];  /* what the "transfer" leaves in +0x0c */
static uint32_t block_header_fill;  /* read: header[k] = fill + k + 16*i */

uint32_t MST_$WIRE(uint32_t vpn, status_$t *status_ret)
{
    if (wire_calls < 16) {
        wire_vas[wire_calls] = vpn;
    }
    if (wire_calls == wire_fail_at) {
        *status_ret = wire_fail_status;
    } else {
        *status_ret = status_$ok;
    }
    wire_calls++;
    return vpn + 0x1000;
}

void WP_$UNWIRE(uint32_t wired)
{
    if (unwire_calls < 16) {
        unwired[unwire_calls] = wired;
    }
    unwire_calls++;
}

void CACHE_$FLUSH_VIRTUAL(void) { flush_calls++; }

void DISK_$GET_QBLKS(int16_t count, uint32_t *qblk_head, uint32_t *qblk_tail)
{
    int i;
    get_qblks_calls++;
    get_qblks_count = count;
    for (i = 0; i < count; i++) {
        memset(BLOCK(i), 0, sizeof(disk_io_req_t));
        BLOCK(i)->next = (i + 1 < count) ? BLOCK_VA(i + 1) : 0;
    }
    *qblk_head = BLOCK_VA(0);
    *qblk_tail = BLOCK_VA(count - 1);
}

void DISK_$RTN_QBLKS(int16_t count, uint32_t qblk_head, uint32_t qblk_tail)
{
    (void)qblk_head;
    (void)qblk_tail;
    rtn_qblks_calls++;
    rtn_qblks_count = count;
}

/* Both "transfers" relink the blocks along free_next (+0x08), the chain
 * the collector walks, and plant a status in each. */
static void relink_and_stamp(int count, int stamp_headers)
{
    int i, k;
    for (i = 0; i < count; i++) {
        BLOCK(i)->free_next = (i + 1 < count) ? BLOCK_VA(i + 1) : 0;
        BLOCK(i)->status = block_status[i];
        if (stamp_headers) {
            for (k = 0; k < 8; k++) {
                BLOCK(i)->header[k] = block_header_fill + (uint32_t)(k + 16 * i);
            }
        }
    }
}

void DISK_$READ_MULTI(uint16_t vol_idx, int16_t flags1, int16_t flags2,
                      int32_t qblk_head, uint32_t qblk_tail,
                      int16_t *pages_read, status_$t *status)
{
    (void)vol_idx; (void)flags1; (void)flags2; (void)qblk_head; (void)qblk_tail;
    read_multi_calls++;
    relink_and_stamp(get_qblks_count, 1);
    *pages_read = read_multi_pages_done;
    *status = read_multi_status;
}

void DISK_$WRITE_MULTI(int8_t flags, void *req_list, status_$t *status)
{
    (void)flags; (void)req_list;
    write_multi_calls++;
    relink_and_stamp(get_qblks_count, 0);
    *status = write_multi_status;
}

#include "../as_xfer_multi.c"

static uint16_t vol_idx;
static int16_t count;
static int16_t op_type;
static uint32_t daddrs[16];
static uint32_t headers[16][8];
static uint32_t *header_ptrs[16];
static uint32_t buffers[16];
static uint32_t page_status[16];
static status_$t status;

static void reset(int n)
{
    int i, k;
    ARCH_HOST_VA_BASE = (uintptr_t)arena;
    memset(arena, 0, sizeof arena);
    wire_calls = unwire_calls = flush_calls = 0;
    wire_fail_at = -1;
    wire_fail_status = 0x00040002;
    get_qblks_calls = rtn_qblks_calls = 0;
    get_qblks_count = rtn_qblks_count = 0;
    read_multi_calls = write_multi_calls = 0;
    read_multi_pages_done = (int16_t)n;
    read_multi_status = status_$ok;
    write_multi_status = status_$ok;
    block_header_fill = 0x1000;
    vol_idx = 3;
    count = (int16_t)n;
    op_type = 0;
    status = 0x11111111;
    for (i = 0; i < 16; i++) {
        daddrs[i] = 0x100u + (uint32_t)i;
        buffers[i] = 0x20000u + 0x400u * (uint32_t)i;
        header_ptrs[i] = headers[i];
        page_status[i] = 0x22222222;
        block_status[i] = 0;
        for (k = 0; k < 8; k++) {
            headers[i][k] = 0xAA00u + (uint32_t)(k + 16 * i);
        }
    }
}

static void run(void)
{
    DISK_$AS_XFER_MULTI(&vol_idx, &count, &op_type, daddrs, header_ptrs,
                        buffers, page_status, &status);
}

/* ============================================================================
 * Tests
 * ============================================================================ */

TEST(read_copies_headers_out_and_status_back)
{
    reset(3);
    block_status[1] = 0x00080011;
    run();
    ASSERT_EQ(3, wire_calls);
    ASSERT_EQ(1, flush_calls);
    ASSERT_EQ(1, read_multi_calls);
    ASSERT_EQ(0, write_multi_calls);
    ASSERT_EQ(3, unwire_calls);
    ASSERT_EQ(1, rtn_qblks_calls);
    ASSERT_EQ(3, rtn_qblks_count);
    /* per-page status from the blocks' +0x0c */
    ASSERT_EQ(0, page_status[0]);
    ASSERT_EQ(0x00080011, page_status[1]);
    ASSERT_EQ(0, page_status[2]);
    /* headers copied out for op_type 0 */
    ASSERT_EQ(0x1000 + 0, headers[0][0]);
    ASSERT_EQ(0x1000 + 7, headers[0][7]);
    ASSERT_EQ(0x1000 + 16 + 3, headers[1][3]);
    ASSERT_EQ(0x1000 + 32 + 7, headers[2][7]);
    ASSERT_EQ(0, status);
    /* the fill loop was requested; the request blocks carried daddr/ppn */
    ASSERT_EQ(0x101, BLOCK(1)->daddr);
    ASSERT_EQ(0x20400 + 0x1000, BLOCK(1)->ppn);
}

TEST(write_copies_headers_in_and_marks_all_done)
{
    reset(2);
    op_type = 1;
    run();
    ASSERT_EQ(1, write_multi_calls);
    ASSERT_EQ(0, read_multi_calls);
    /* headers went IN to the blocks, low byte of vol_idx in op_flags */
    ASSERT_EQ(0xAA00 + 0, BLOCK(0)->header[0]);
    ASSERT_EQ(0xAA00 + 16 + 5, BLOCK(1)->header[5]);
    ASSERT_EQ(3, BLOCK(0)->op_flags);
    /* the caller's headers are untouched on a write */
    ASSERT_EQ(0xAA00 + 0, headers[0][0]);
    /* pages_done = count, so nothing is marked not-executed */
    ASSERT_EQ(0, page_status[0]);
    ASSERT_EQ(0, page_status[1]);
    ASSERT_EQ(0, status);
}

/* `tst.w D4w; bne` at 0x00E6BB50 / 0x00E6BBB4: an op_type that is neither
 * 0 nor 1 goes down the READ_MULTI path but copies no headers out. */
TEST(other_op_type_reads_without_header_copy_out)
{
    reset(2);
    op_type = 5;
    run();
    ASSERT_EQ(1, read_multi_calls);
    ASSERT_EQ(0, write_multi_calls);
    ASSERT_EQ(0xAA00 + 0, headers[0][0]);
    ASSERT_EQ(0xAA00 + 16 + 7, headers[1][7]);
    ASSERT_EQ(0, page_status[0]);
    ASSERT_EQ(0, page_status[1]);
}

TEST(short_read_marks_remaining_pages_not_executed)
{
    reset(4);
    read_multi_pages_done = 2;
    read_multi_status = 0x00080031;
    run();
    ASSERT_EQ(0, page_status[0]);
    ASSERT_EQ(0, page_status[1]);
    ASSERT_EQ(status_$disk_transfer_not_executed, page_status[2]);
    ASSERT_EQ(status_$disk_transfer_not_executed, page_status[3]);
    ASSERT_EQ(0x00080031, status);
}

TEST(misaligned_buffer_stops_before_wiring)
{
    reset(3);
    buffers[1] = 0x20401;
    run();
    ASSERT_EQ(0, wire_calls);
    ASSERT_EQ(0, get_qblks_calls);
    ASSERT_EQ(0, unwire_calls);
    ASSERT_EQ(status_$disk_buffer_not_page_aligned, status);
    /* pages_done stayed 0: every page is "transfer not executed" */
    ASSERT_EQ(status_$disk_transfer_not_executed, page_status[0]);
    ASSERT_EQ(status_$disk_transfer_not_executed, page_status[1]);
    ASSERT_EQ(status_$disk_transfer_not_executed, page_status[2]);
}

TEST(wire_failure_unwires_earlier_pages_only)
{
    reset(3);
    wire_fail_at = 2;
    run();
    ASSERT_EQ(3, wire_calls);
    ASSERT_EQ(2, unwire_calls);
    ASSERT_EQ(0x20000 + 0x1000, unwired[0]);
    ASSERT_EQ(0x20400 + 0x1000, unwired[1]);
    ASSERT_EQ(0, get_qblks_calls);
    ASSERT_EQ(0x00040002, status);
    ASSERT_EQ(status_$disk_transfer_not_executed, page_status[0]);
    ASSERT_EQ(status_$disk_transfer_not_executed, page_status[2]);
}

TEST(wire_failure_on_first_page_unwires_nothing)
{
    reset(2);
    wire_fail_at = 0;
    run();
    ASSERT_EQ(1, wire_calls);
    ASSERT_EQ(0, unwire_calls);
    ASSERT_EQ(0x00040002, status);
}

TEST(zero_count_does_nothing_but_the_calls)
{
    reset(0);
    run();
    ASSERT_EQ(0, wire_calls);
    ASSERT_EQ(1, flush_calls);
    ASSERT_EQ(1, get_qblks_calls);
    ASSERT_EQ(1, read_multi_calls);
    ASSERT_EQ(1, rtn_qblks_calls);
    ASSERT_EQ(0x22222222, page_status[0]);
    ASSERT_EQ(0, status);
}

int main(void)
{
    printf("test_as_xfer_multi:\n");
    RUN_TEST(read_copies_headers_out_and_status_back);
    RUN_TEST(write_copies_headers_in_and_marks_all_done);
    RUN_TEST(other_op_type_reads_without_header_copy_out);
    RUN_TEST(short_read_marks_remaining_pages_not_executed);
    RUN_TEST(misaligned_buffer_stops_before_wiring);
    RUN_TEST(wire_failure_unwires_earlier_pages_only);
    RUN_TEST(wire_failure_on_first_page_unwires_nothing);
    RUN_TEST(zero_count_does_nothing_but_the_calls);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
