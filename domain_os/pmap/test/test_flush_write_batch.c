/*
 * pmap/test/test_flush_write_batch.c - Unit tests for pmap_$flush_write_batch
 *
 * Tests the batch write logic by mocking all external dependencies.
 * The DISK_$GET_QBLKS mock writes full pointer-width values through
 * a macro so the test works on both 32-bit (m68k) and 64-bit hosts.
 */

#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

/* Test infrastructure */
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  Running %s... ", #name); \
    reset_mocks(); \
    test_##name(); \
    tests_passed++; \
    printf("PASSED\n"); \
} while(0)

#define ASSERT_EQ(expected, actual) do { \
    if ((expected) != (actual)) { \
        printf("FAILED\n    Expected: 0x%lx, Got: 0x%lx at line %d\n", \
               (unsigned long)(expected), (unsigned long)(actual), __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        printf("FAILED\n    Assertion failed at line %d: %s\n", __LINE__, #cond); \
        tests_failed++; \
        return; \
    } \
} while(0)

/*
 * ========================================================================
 * Mock types
 * ========================================================================
 */

typedef uint32_t status_$t;

/* Avoid conflict with system uid_t on macOS/Linux */
typedef struct { uint32_t high; uint32_t low; } domain_uid_t;
#define uid_t domain_uid_t

/* mmape_t from mmap.h */
typedef struct mmape_t {
    uint8_t  wire_count;
    uint8_t  seg_offset;
    uint16_t segment;
    uint8_t  wsl_index;
    uint8_t  flags1;
    uint16_t prev_vpn;
    uint8_t  priority;
    uint8_t  flags2;
    uint16_t next_vpn;
    uint32_t disk_addr;
} mmape_t;

/* ec_$eventcount_t from ec.h */
typedef struct ec_$eventcount_t {
    uint32_t value;
    uint32_t pad1;
    uint32_t pad2;
} ec_$eventcount_t;

/* Process stats */
#define PROC1_MAX_PROCESSES 65
uint32_t PROC_STATS_BASE[PROC1_MAX_PROCESSES * 4];
uint16_t PROC1_$CURRENT;

/* NETLOG */
int8_t NETLOG_$OK_TO_LOG;

/* MMAPE array */
#define MOCK_MMAPE_SIZE 4096
static mmape_t mock_mmape[MOCK_MMAPE_SIZE];

/* AST event count */
ec_$eventcount_t mock_pmap_in_trans_ec;

/* Simulated queue block structure (64 bytes per block) */
#define QBLK_SIZE 64
#define MAX_QBLKS 16
static uint8_t mock_qblks[MAX_QBLKS][QBLK_SIZE];

/*
 * Side table for qblk chain links. On 64-bit hosts, storing 8-byte pointers
 * at offset 0x08 in a 64-byte qblk overlaps the 4-byte status at 0x0C.
 * We store the next-pointers separately and override QBLK_GET_NEXT.
 */
static uint8_t *mock_qblk_next[MAX_QBLKS];

static int qblk_index_of(uint8_t *qblk) {
    return (int)((qblk - mock_qblks[0]) / QBLK_SIZE);
}
#define QBLK_GET_NEXT(qblk) (mock_qblk_next[qblk_index_of(qblk)])

/*
 * ========================================================================
 * Mock call tracking
 * ========================================================================
 */

static int mock_ml_lock_count;
static int mock_ml_unlock_count;
static uint16_t mock_ml_lock_ids[16];
static uint16_t mock_ml_unlock_ids[16];

static int mock_get_qblks_called;
static int16_t mock_get_qblks_count;

static int mock_fill_called;
static int32_t *mock_fill_pages;
static uint32_t *mock_fill_qblk;
static int16_t mock_fill_count;

static int mock_write_multi_called;
static int8_t mock_write_multi_flags;
static status_$t mock_write_multi_result;

static int mock_write_complete_count;
static int32_t mock_write_complete_vpns[MAX_QBLKS];

static int mock_update_seg_map_count;
static uint32_t mock_update_seg_map_vpns[MAX_QBLKS];
static uint16_t mock_update_seg_map_pages[MAX_QBLKS];

static int mock_ec_advance_count;

static int mock_rtn_qblks_called;
static int16_t mock_rtn_qblks_count;

static int mock_crash_called;

static int mock_netlog_count;
static uint16_t mock_netlog_kinds[MAX_QBLKS];

static void reset_mocks(void)
{
    memset(PROC_STATS_BASE, 0, sizeof(PROC_STATS_BASE));
    PROC1_$CURRENT = 1;
    NETLOG_$OK_TO_LOG = 0;
    memset(mock_mmape, 0, sizeof(mock_mmape));
    memset(&mock_pmap_in_trans_ec, 0, sizeof(mock_pmap_in_trans_ec));
    memset(mock_qblks, 0, sizeof(mock_qblks));

    mock_ml_lock_count = 0;
    mock_ml_unlock_count = 0;
    mock_get_qblks_called = 0;
    mock_fill_called = 0;
    mock_write_multi_called = 0;
    mock_write_multi_result = 0;
    mock_write_complete_count = 0;
    mock_update_seg_map_count = 0;
    mock_ec_advance_count = 0;
    mock_rtn_qblks_called = 0;
    mock_crash_called = 0;
    mock_netlog_count = 0;

    /* Set up qblk result chain via side table (avoids 64-bit pointer overlap) */
    for (int i = 0; i < MAX_QBLKS - 1; i++) {
        mock_qblk_next[i] = mock_qblks[i + 1];
    }
    mock_qblk_next[MAX_QBLKS - 1] = NULL;
}

/*
 * ========================================================================
 * Mock function implementations and header overrides
 * ========================================================================
 */

/* Prevent real headers from being included */
#define PMAP_INTERNAL_H
#define PMAP_H
#define BASE_H
#define EC_H
#define ML_H
#define DISK_H
#define AST_H
#define NETLOG_H
#define MISC_H
#define MMAP_H
#define MMU_H
#define TIME_H
#define PROC1_H
#define PROC1_CONFIG_H
#define NETWORK_H

/* Provide macros the source needs */
#define PMAP_LOCK_ID 0x14
#define MMAPE_BASE mock_mmape
#define MMAPE_FOR_VPN(vpn) (&mock_mmape[(vpn)])
#define AST_$PMAP_IN_TRANS_EC mock_pmap_in_trans_ec

void ML_$LOCK(uint16_t id) {
    mock_ml_lock_ids[mock_ml_lock_count++] = id;
}

void ML_$UNLOCK(uint16_t id) {
    mock_ml_unlock_ids[mock_ml_unlock_count++] = id;
}

/*
 * DISK_$GET_QBLKS mock: writes dummy raw values (truncated on 64-bit, but
 * the implementation converts via DISK_QBLK_RAW_TO_PTR which we override
 * below to return the full-width mock pointer).
 */
static uint8_t *mock_qblk_head_ptr;
static uint8_t *mock_qblk_tail_ptr;

#define DISK_$GET_QBLKS(count, headp, tailp) do { \
    mock_get_qblks_called = 1; \
    mock_get_qblks_count = (count); \
    *(headp) = 0; \
    *(tailp) = 0; \
} while(0)

/*
 * Override DISK_QBLK_RAW_TO_PTR to return the full-width mock pointer.
 * This avoids the int32_t truncation issue on 64-bit hosts.
 */
#define DISK_QBLK_RAW_TO_PTR(raw) (mock_qblk_head_ptr)

#define DISK_$RTN_QBLKS(count, head, tail) do { \
    mock_rtn_qblks_called = 1; \
    mock_rtn_qblks_count = (count); \
} while(0)

void DISK_$WRITE_MULTI(int8_t flags, void *req_list, status_$t *status) {
    mock_write_multi_called = 1;
    mock_write_multi_flags = flags;
    *status = mock_write_multi_result;
}

void pmap_$fill_write_qblks(int32_t *pages, uint32_t *qblk, int16_t count) {
    mock_fill_called = 1;
    mock_fill_pages = pages;
    mock_fill_qblk = qblk;
    mock_fill_count = count;
}

void pmap_$write_complete(int32_t vpn, void *status_ptr) {
    if (mock_write_complete_count < MAX_QBLKS) {
        mock_write_complete_vpns[mock_write_complete_count] = vpn;
    }
    mock_write_complete_count++;
}

void pmap_$update_seg_map(uint16_t *segmap_entry, uint32_t vpn, uint16_t page_idx) {
    if (mock_update_seg_map_count < MAX_QBLKS) {
        mock_update_seg_map_vpns[mock_update_seg_map_count] = vpn;
        mock_update_seg_map_pages[mock_update_seg_map_count] = page_idx;
    }
    mock_update_seg_map_count++;
}

void EC_$ADVANCE(ec_$eventcount_t *ec) {
    mock_ec_advance_count++;
    ec->value++;
}

void CRASH_SYSTEM(const status_$t *status) {
    mock_crash_called = 1;
}

void NETLOG_$LOG_IT(uint16_t kind, uint32_t *uid_,
                    uint16_t p3, uint16_t p4,
                    uint16_t p5, uint16_t p6,
                    uint16_t p7, uint16_t p8) {
    if (mock_netlog_count < MAX_QBLKS) {
        mock_netlog_kinds[mock_netlog_count] = kind;
    }
    mock_netlog_count++;
}

/* Include the implementation under test */
#include "../flush_write_batch.c"

/*
 * ========================================================================
 * Helpers
 * ========================================================================
 */

static void setup_qblk(int idx, int32_t vpn, int32_t write_status)
{
    *(int32_t *)(mock_qblks[idx] + 0x14) = vpn;
    *(int32_t *)(mock_qblks[idx] + 0x0C) = write_status;
    *(uint32_t *)(mock_qblks[idx] + 0x28) = 0x60;  /* block-in-seg (>> 5 = 3) */
}

static void setup_mock_qblk_chain(void)
{
    mock_qblk_head_ptr = mock_qblks[0];
    mock_qblk_tail_ptr = mock_qblks[0];
}

/*
 * ========================================================================
 * Test cases
 * ========================================================================
 */

TEST(single_page_success)
{
    int16_t batch_count = 1;
    uint32_t batch_vpns[16] = { 0x300 };
    uint32_t segmap[32] = { 0 };
    status_$t status = 0;

    mock_mmape[0x300].seg_offset = 5;
    setup_qblk(0, 0x300, 0);
    setup_mock_qblk_chain();

    pmap_$flush_write_batch(&batch_count, batch_vpns, segmap, &status);

    /* Verify unlock then lock sequence */
    ASSERT_EQ(1, mock_ml_unlock_count);
    ASSERT_EQ(PMAP_LOCK_ID, mock_ml_unlock_ids[0]);
    ASSERT_EQ(1, mock_ml_lock_count);
    ASSERT_EQ(PMAP_LOCK_ID, mock_ml_lock_ids[0]);

    /* Verify DISK_$GET_QBLKS called */
    ASSERT_TRUE(mock_get_qblks_called);

    /* Verify fill_write_qblks called */
    ASSERT_TRUE(mock_fill_called);
    ASSERT_EQ(1, mock_fill_count);

    /* Verify WRITE_MULTI called with flags = -1 */
    ASSERT_TRUE(mock_write_multi_called);
    ASSERT_EQ(-1, mock_write_multi_flags);

    /* Verify write_complete called */
    ASSERT_EQ(1, mock_write_complete_count);
    ASSERT_EQ(0x300, mock_write_complete_vpns[0]);

    /* Verify stats incremented */
    ASSERT_EQ(1, PROC_STATS_BASE[PROC1_$CURRENT * 4 + 2]);

    /* Verify update_seg_map called with correct args */
    ASSERT_EQ(1, mock_update_seg_map_count);
    ASSERT_EQ(0x300, mock_update_seg_map_vpns[0]);
    ASSERT_EQ(5, mock_update_seg_map_pages[0]);

    /* Verify EC_$ADVANCE called */
    ASSERT_EQ(1, mock_ec_advance_count);

    /* Verify RTN_QBLKS called */
    ASSERT_TRUE(mock_rtn_qblks_called);

    /* Verify batch_count cleared */
    ASSERT_EQ(0, batch_count);

    /* Verify status still 0 */
    ASSERT_EQ(0, status);
}

TEST(multi_page_batch)
{
    int16_t batch_count = 3;
    uint32_t batch_vpns[16] = { 0x300, 0x301, 0x302 };
    uint32_t segmap[32] = { 0 };
    status_$t status = 0;

    mock_mmape[0x300].seg_offset = 5;
    mock_mmape[0x301].seg_offset = 6;
    mock_mmape[0x302].seg_offset = 7;

    setup_qblk(0, 0x300, 0);
    setup_qblk(1, 0x301, 0);
    setup_qblk(2, 0x302, 0);
    setup_mock_qblk_chain();

    pmap_$flush_write_batch(&batch_count, batch_vpns, segmap, &status);

    ASSERT_EQ(3, mock_write_complete_count);
    ASSERT_EQ(3, mock_update_seg_map_count);
    ASSERT_EQ(3, PROC_STATS_BASE[PROC1_$CURRENT * 4 + 2]);
    ASSERT_EQ(0, batch_count);
    ASSERT_EQ(0, status);
}

TEST(write_error_propagates)
{
    int16_t batch_count = 2;
    uint32_t batch_vpns[16] = { 0x300, 0x301 };
    uint32_t segmap[32] = { 0 };
    status_$t status = 0;

    mock_mmape[0x300].seg_offset = 5;
    mock_mmape[0x301].seg_offset = 6;

    /* First page succeeds, second has error */
    setup_qblk(0, 0x300, 0);
    setup_qblk(1, 0x301, 0x50007);
    setup_mock_qblk_chain();

    pmap_$flush_write_batch(&batch_count, batch_vpns, segmap, &status);

    ASSERT_EQ(2, mock_write_complete_count);
    ASSERT_EQ(1, mock_update_seg_map_count);
    ASSERT_EQ(1, PROC_STATS_BASE[PROC1_$CURRENT * 4 + 2]);
    ASSERT_EQ(0x50007, status);
    ASSERT_EQ(0, batch_count);
}

TEST(write_status_minus_one_ignored)
{
    int16_t batch_count = 1;
    uint32_t batch_vpns[16] = { 0x300 };
    uint32_t segmap[32] = { 0 };
    status_$t status = 0;

    setup_qblk(0, 0x300, -1);
    setup_mock_qblk_chain();

    pmap_$flush_write_batch(&batch_count, batch_vpns, segmap, &status);

    ASSERT_EQ(1, mock_write_complete_count);
    ASSERT_EQ(0, mock_update_seg_map_count);
    ASSERT_EQ(0, PROC_STATS_BASE[PROC1_$CURRENT * 4 + 2]);
    ASSERT_EQ(0, status);  /* -1 sentinel NOT propagated */
}

TEST(write_multi_failure_crashes)
{
    int16_t batch_count = 1;
    uint32_t batch_vpns[16] = { 0x300 };
    uint32_t segmap[32] = { 0 };
    status_$t status = 0;

    mock_write_multi_result = 0xDEAD;
    setup_mock_qblk_chain();

    pmap_$flush_write_batch(&batch_count, batch_vpns, segmap, &status);

    ASSERT_TRUE(mock_crash_called);
}

TEST(empty_batch)
{
    int16_t batch_count = 0;
    uint32_t batch_vpns[16] = { 0 };
    uint32_t segmap[32] = { 0 };
    status_$t status = 0;

    setup_mock_qblk_chain();

    pmap_$flush_write_batch(&batch_count, batch_vpns, segmap, &status);

    ASSERT_EQ(1, mock_ml_unlock_count);
    ASSERT_EQ(1, mock_ml_lock_count);
    ASSERT_TRUE(mock_get_qblks_called);
    ASSERT_TRUE(mock_write_multi_called);
    ASSERT_EQ(1, mock_ec_advance_count);
    ASSERT_TRUE(mock_rtn_qblks_called);
    ASSERT_EQ(0, mock_write_complete_count);
    ASSERT_EQ(0, mock_update_seg_map_count);
    ASSERT_EQ(0, batch_count);
}

TEST(netlog_when_enabled)
{
    int16_t batch_count = 1;
    uint32_t batch_vpns[16] = { 0x300 };
    uint32_t segmap[32] = { 0 };
    status_$t status = 0;

    NETLOG_$OK_TO_LOG = -1;  /* bit 7 set = enabled */
    mock_mmape[0x300].seg_offset = 5;
    setup_qblk(0, 0x300, 0);
    setup_mock_qblk_chain();

    pmap_$flush_write_batch(&batch_count, batch_vpns, segmap, &status);

    ASSERT_EQ(1, mock_netlog_count);
    ASSERT_EQ(3, mock_netlog_kinds[0]);
}

TEST(netlog_when_disabled)
{
    int16_t batch_count = 1;
    uint32_t batch_vpns[16] = { 0x300 };
    uint32_t segmap[32] = { 0 };
    status_$t status = 0;

    NETLOG_$OK_TO_LOG = 0;  /* disabled */
    mock_mmape[0x300].seg_offset = 5;
    setup_qblk(0, 0x300, 0);
    setup_mock_qblk_chain();

    pmap_$flush_write_batch(&batch_count, batch_vpns, segmap, &status);

    ASSERT_EQ(0, mock_netlog_count);
}

TEST(process_stats_tracking)
{
    int16_t batch_count = 2;
    uint32_t batch_vpns[16] = { 0x300, 0x301 };
    uint32_t segmap[32] = { 0 };
    status_$t status = 0;

    PROC1_$CURRENT = 3;
    mock_mmape[0x300].seg_offset = 5;
    mock_mmape[0x301].seg_offset = 6;

    setup_qblk(0, 0x300, 0);
    setup_qblk(1, 0x301, 0);
    setup_mock_qblk_chain();

    pmap_$flush_write_batch(&batch_count, batch_vpns, segmap, &status);

    ASSERT_EQ(2, PROC_STATS_BASE[3 * 4 + 2]);
    ASSERT_EQ(0, PROC_STATS_BASE[0 * 4 + 2]);
    ASSERT_EQ(0, PROC_STATS_BASE[1 * 4 + 2]);
}

TEST(lock_ordering)
{
    int16_t batch_count = 1;
    uint32_t batch_vpns[16] = { 0x300 };
    uint32_t segmap[32] = { 0 };
    status_$t status = 0;

    mock_mmape[0x300].seg_offset = 0;
    setup_qblk(0, 0x300, 0);
    setup_mock_qblk_chain();

    pmap_$flush_write_batch(&batch_count, batch_vpns, segmap, &status);

    ASSERT_EQ(1, mock_ml_unlock_count);
    ASSERT_EQ(1, mock_ml_lock_count);
    ASSERT_EQ(PMAP_LOCK_ID, mock_ml_unlock_ids[0]);
    ASSERT_EQ(PMAP_LOCK_ID, mock_ml_lock_ids[0]);
}

/*
 * ========================================================================
 * Main
 * ========================================================================
 */

int main(void)
{
    printf("Running pmap_$flush_write_batch tests...\n\n");

    RUN_TEST(single_page_success);
    RUN_TEST(multi_page_batch);
    RUN_TEST(write_error_propagates);
    RUN_TEST(write_status_minus_one_ignored);
    RUN_TEST(write_multi_failure_crashes);
    RUN_TEST(empty_batch);
    RUN_TEST(netlog_when_enabled);
    RUN_TEST(netlog_when_disabled);
    RUN_TEST(process_stats_tracking);
    RUN_TEST(lock_ordering);

    printf("\n%d tests passed, %d tests failed\n",
           tests_passed, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
