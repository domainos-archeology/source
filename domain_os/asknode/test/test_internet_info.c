/*
 * asknode/test/test_internet_info.c - unit tests for ASKNODE_$INTERNET_INFO
 *                                     (0x00E645EA)
 *
 * These tests #include asknode/internet_info.c and drive the real function
 * through mocked callees, so what is exercised is the emitted translation and
 * not a re-implementation.
 *
 * What they pin down (bead source-1b7z):
 *
 *   - the memory-statistics copy at 0x00E64812-0x00E6482A moves the WHOLE
 *     0x56-byte MEM_$MEM_REC (21 longwords + one word) to reply+0x72, not the
 *     21 words the tree used to copy, so the per-page parity-error table
 *     reaches the reply;
 *   - the MMAP_$REAL_PAGES word at reply+0x76 (0x00E6483A / 0x00E64842) is
 *     stored AFTER that copy and therefore overwrites two of its bytes;
 *   - the negated-uid store at reply+0x12 (0x00E648CC-0x00E648D2) that the
 *     node-uid and root-uid arms make;
 *   - the unknown-request arm at 0x00E65502 writes the caller's status_ret,
 *     leaving reply+0x04 zero.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

/* ==========================================================================
 * Test infrastructure
 * ========================================================================== */

static int tests_passed = 0;
static int tests_failed = 0;
static int current_failed = 0;
static void reset_mocks(void);

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do {                                                   \
    printf("  Running %s... ", #name);                                        \
    current_failed = 0;                                                       \
    reset_mocks();                                                            \
    test_##name();                                                            \
    if (current_failed == 0) { tests_passed++; printf("PASSED\n"); }          \
} while (0)

#define ASSERT_EQ(expected, actual) do {                                      \
    if ((unsigned long long)(expected) != (unsigned long long)(actual)) {     \
        printf("FAILED\n    Expected: 0x%llx, Got: 0x%llx at line %d\n",      \
               (unsigned long long)(expected),                                \
               (unsigned long long)(actual), __LINE__);                       \
        tests_failed++; current_failed = 1;                                   \
        return;                                                               \
    }                                                                         \
} while (0)

#define ASSERT_TRUE(cond) do {                                                \
    if (!(cond)) {                                                            \
        printf("FAILED\n    Assertion failed at line %d: %s\n",               \
               __LINE__, #cond);                                              \
        tests_failed++; current_failed = 1;                                   \
        return;                                                               \
    }                                                                         \
} while (0)

/* ==========================================================================
 * Headers the translation unit under test needs
 * ========================================================================== */

#include "asknode/asknode_internal.h"

/* ==========================================================================
 * Kernel data the function reads
 * ========================================================================== */

uint32_t NODE_$ME;
uint32_t TIME_$BOOT_TIME;
uint32_t TIME_$CURRENT_CLOCKH;
uint32_t ROUTE_$PORT;
mmap_globals_t MMAP_GLOBALS_STORAGE;    /* MMAP_$REAL_PAGES lives in here */
mem_data_t MEM_DATA;
ring_global_t RING_$CTL;
ring_$stats_t RING_$DATA[RING_MAX_UNITS];  /* 0xE261E0: the 15-longword copy source */
cal_$timezone_rec_t CAL_$TIMEZONE;
name_$data_t NAME_$DATA;                /* NAME_$ROOT_UID lives in here */
uid_t UID_$NIL;
uint32_t NETWORK_$MOTHER_NODE;
uint32_t NETWORK_$ALLOWED_SERVICE;
uid_t NETWORK_$PAGING_FILE_UID;
int8_t NETWORK_$DISKLESS;
uint16_t NETWORK_$INFO_RQST_CNT;
uint16_t NETWORK_$MULT_PAGIN_RQST_CNT;
uint16_t NETWORK_$PAGIN_RQST_CNT;
uint16_t NETWORK_$PAGOUT_RQST_CNT;
uint16_t NETWORK_$READ_CALL_CNT;
uint16_t NETWORK_$WRITE_CALL_CNT;
uint16_t NETWORK_$READ_VIOL_CNT;
uint16_t NETWORK_$WRITE_VIOL_CNT;
uint16_t NETWORK_$BAD_CHKSUM_CNT;
uint16_t ASKNODE_$PROTOCOL_VERSION;
uint32_t ASKNODE_$EMPTY_DATA;
uint32_t PKT_$DEFAULT_INFO[8];

/* ==========================================================================
 * Mock state
 * ========================================================================== */

/*
 * The reply record.  0xC8 bytes is exactly the end of the memory-statistics
 * copy (0x72 + 0x56); the array is longer so an over-copy is visible, and it
 * is a uint32_t array so the record is aligned the way a kernel reply is.
 */
#define REPLY_LONGS 0x40
static uint32_t reply[REPLY_LONGS];
static uint8_t  *reply_bytes = (uint8_t *)reply;

static uid_t     mock_node_uid;
static uid_t     mock_root_uid;
static int       mock_disk_get_stats_calls;

static void reset_mocks(void)
{
    memset(reply, 0, sizeof(reply));
    memset(&MEM_DATA, 0, sizeof(MEM_DATA));
    memset(&RING_$CTL, 0, sizeof(RING_$CTL));
    memset(RING_$DATA, 0, sizeof(RING_$DATA));
    memset(&CAL_$TIMEZONE, 0, sizeof(CAL_$TIMEZONE));
    memset(&MMAP_GLOBALS_STORAGE, 0, sizeof(MMAP_GLOBALS_STORAGE));
    NODE_$ME = 0x00012345;
    TIME_$BOOT_TIME = 0;
    TIME_$CURRENT_CLOCKH = 0;
    ROUTE_$PORT = 0;
    MMAP_$REAL_PAGES = 0;
    NETWORK_$DISKLESS = 0;
    NETWORK_$MOTHER_NODE = 0;
    mock_node_uid.high = 0;
    mock_node_uid.low = 0;
    mock_root_uid.high = 0;
    mock_root_uid.low = 0;
    mock_disk_get_stats_calls = 0;
}

/* ==========================================================================
 * Mocked callees
 * ========================================================================== */

void NAME_$GET_NODE_UID(uid_t *node_uid) { *node_uid = mock_node_uid; }
void NAME_$GET_ROOT_UID(uid_t *root_uid) { *root_uid = mock_root_uid; }

void DISK_$GET_STATS(int16_t dev_type, int16_t controller, uint8_t *has_stats,
                     void *stats)
{
    (void)dev_type; (void)controller; (void)stats;
    mock_disk_get_stats_calls++;
    *has_stats = 0;
}

void VOLX_$GET_INFO(int16_t *vol_idx, uid_t *dir_uid_ret, uint32_t *free_blocks,
                    uint32_t *total_blocks, status_$t *status_ret)
{
    (void)vol_idx; (void)dir_uid_ret; (void)free_blocks; (void)total_blocks;
    *status_ret = 0;
}

void PROC2_$LIST(uid_t *uid_list, uint16_t *max_count, uint16_t *count)
{ (void)uid_list; (void)max_count; *count = 0; }

void PROC2_$GET_INFO(uid_t *proc_uid, void *info, uint16_t *info_len,
                     status_$t *status_ret)
{ (void)proc_uid; (void)info; (void)info_len; *status_ret = 0; }

void PROC2_$SIGNAL_PGROUP_OS(uid_t *pgroup_uid, int16_t *signal,
                             uint32_t *flags, status_$t *status_ret)
{ (void)pgroup_uid; (void)signal; (void)flags; *status_ret = 0; }

void GET_BUILD_TIME(char *buf, int16_t *len_p) { (void)buf; *len_p = 0; }

void LOG_$READ(void *buffer, uint16_t *max_len, uint16_t *actual_len)
{ (void)buffer; (void)max_len; *actual_len = 0; }

void LOG_$READ2(void *buffer, uint16_t offset, uint16_t max_len,
                uint16_t *actual_len)
{ (void)buffer; (void)offset; (void)max_len; *actual_len = 0; }

void NETWORK_$RING_INFO(void *net_handle, ring_info_t *ring_info,
                        status_$t *status_ret)
{ (void)net_handle; (void)ring_info; *status_ret = 0; }

uint32_t DIR_$FIND_NET(uid_t *dir_uid, uint32_t *index)
{ (void)dir_uid; (void)index; return 0; }

int16_t HINT_$GET_HINTS(uid_t *file_uid, uint32_t *addresses)
{ (void)file_uid; addresses[0] = 0; return 0; }

void HINT_$ADDI(uid_t *uid_ptr, uint32_t *addresses)
{ (void)uid_ptr; (void)addresses; }

void PKT_$SAR_INTERNET(uint32_t routing_key, uint32_t dest_node, uint16_t dest_sock,
                       void *pkt_info, int16_t timeout,
                       void *req_template, uint16_t req_tpl_len,
                       void *req_data, uint16_t req_data_len,
                       void *resp_buf, char *resp_tpl_buf, uint16_t resp_tpl_max,
                       uint16_t *resp_tpl_len, void *resp_data_buf,
                       uint16_t resp_data_max, uint16_t *resp_data_len,
                       status_$t *status_ret)
{
    (void)routing_key; (void)dest_node; (void)dest_sock; (void)pkt_info;
    (void)timeout; (void)req_template; (void)req_tpl_len; (void)req_data;
    (void)req_data_len; (void)resp_buf; (void)resp_tpl_buf; (void)resp_tpl_max;
    (void)resp_data_buf; (void)resp_data_max;
    *resp_tpl_len = 0;
    *resp_data_len = 0;
    *status_ret = 0;
}

/* ==========================================================================
 * The translation unit under test
 * ========================================================================== */

#include "../internet_info.c"

/* ==========================================================================
 * Helpers
 * ========================================================================== */

/* Drive a local-node query (node_id == 0 takes the local arm at 0x00E64622). */
static status_$t run_local(uint16_t req, uid_t *param)
{
    uint32_t node_id = 0;
    int32_t  req_len = 0;
    uint16_t resp_len = sizeof(reply);
    status_$t status = 0xdeadbeef;
    uid_t     zero_param = { 0, 0 };

    ASKNODE_$INTERNET_INFO(&req, &node_id, &req_len,
                           param ? param : &zero_param,
                           &resp_len, reply, &status);
    return status;
}

/*
 * Read a reply word the way the emitted code stores it.  The kernel is
 * big-endian but these tests run on the host, so words are compared as
 * values rather than as byte pairs (the record copy above is byte-exact
 * either way, being a straight block move).
 */
static uint16_t reply_word_at(unsigned off)
{
    uint16_t v;
    memcpy(&v, reply_bytes + off, sizeof(v));
    return v;
}

/* Fill MEM_$MEM_REC with a byte pattern that makes every offset distinct. */
static void fill_mem_rec(void)
{
    uint8_t *p = (uint8_t *)&MEM_$MEM_REC;
    unsigned i;
    for (i = 0; i < sizeof(mem_$mem_rec_t); i++) {
        p[i] = (uint8_t)(0xA0 + i);
    }
}

/* ==========================================================================
 * Tests
 * ========================================================================== */

/*
 * mem/mem.h models MEM_$MEM_REC as the 0x56 bytes 0xE22934..0xE22989, which
 * is what the loop at 0x00E6481C-0x00E6482A moves (21 longwords + one word).
 */
TEST(mem_rec_is_the_0x56_bytes_the_loop_moves)
{
    ASSERT_EQ(0x56, sizeof(mem_$mem_rec_t));
    ASSERT_EQ(21 * 4 + 2, sizeof(mem_$mem_rec_t));
    /* The copy lands at reply+0x72 and ends at 0xC8, inside the buffer. */
    ASSERT_TRUE(0x72 + sizeof(mem_$mem_rec_t) <= sizeof(reply));
}

/*
 * 0x00E64812-0x00E6482A: the whole record reaches reply+0x72.  Only the two
 * bytes at +0x76 differ, because the MMAP_$REAL_PAGES store at 0x00E6483A
 * runs after the copy.
 */
TEST(stats_copies_whole_mem_rec_to_reply_0x72)
{
    unsigned i;

    fill_mem_rec();
    MMAP_$REAL_PAGES = 0;           /* stores 0 over +0x76, see next test */
    memset(reply_bytes, 0x5A, sizeof(reply));

    run_local(0x06, NULL);

    for (i = 0; i < sizeof(mem_$mem_rec_t); i++) {
        uint8_t expect = (uint8_t)(0xA0 + i);
        if (0x72 + i == 0x76 || 0x72 + i == 0x77) {
            continue;               /* overwritten by MMAP_$REAL_PAGES */
        }
        if (reply_bytes[0x72 + i] != expect) {
            printf("FAILED\n    reply[0x%02x]: expected 0x%02x, got 0x%02x\n",
                   0x72 + i, expect, reply_bytes[0x72 + i]);
            tests_failed++; current_failed = 1;
            return;
        }
    }
}

/* The copy is exactly 0x56 bytes: neither +0x71 nor +0xC8 is touched. */
TEST(stats_mem_rec_copy_does_not_overrun)
{
    fill_mem_rec();
    memset(reply_bytes, 0x5A, sizeof(reply));

    run_local(0x06, NULL);

    ASSERT_EQ(0x5A, reply_bytes[0x71]);
    ASSERT_EQ(0x5A, reply_bytes[0x72 + 0x56]);
    ASSERT_EQ(0x5A, reply_bytes[0x72 + 0x57]);
}

/*
 * The tail of the record is MEM_$PAGE_ERRORS (mem/mem.h): four 18-byte
 * per-page parity-error records at record offset 0x0E, i.e. reply+0x80.
 * These are the bytes the 21-word copy used to drop entirely.
 */
TEST(stats_reply_carries_the_page_error_table)
{
    unsigned i;

    memset(&MEM_DATA, 0, sizeof(MEM_DATA));
    for (i = 0; i < MEM_PAGE_ERROR_RECORDS; i++) {
        MEM_$PAGE_ERRORS[i].phys_addr = 0x00110000u * (i + 1);
        MEM_$PAGE_ERRORS[i].count = (uint16_t)(i + 1);
    }
    memset(reply_bytes, 0x5A, sizeof(reply));

    run_local(0x06, NULL);

    for (i = 0; i < MEM_PAGE_ERROR_RECORDS; i++) {
        const mem_$page_error_t *out =
            (const mem_$page_error_t *)(reply_bytes + 0x72 + 0x0E + i * 0x12);
        uint32_t phys;
        uint16_t count;
        memcpy(&phys, &out->phys_addr, sizeof(phys));
        memcpy(&count, &out->count, sizeof(count));
        ASSERT_EQ(0x00110000u * (i + 1), phys);
        ASSERT_EQ(i + 1, count);
    }
}

/*
 * 0x00E6482C-0x00E64846: the real-page count is stored at reply+0x76 after
 * the record copy, and is clamped to zero above 0xFFFF.
 */
TEST(stats_real_pages_word_follows_the_copy)
{
    fill_mem_rec();
    MMAP_$REAL_PAGES = 0x1234;
    run_local(0x06, NULL);
    /* the record byte the copy put here (0xA0 + 4) is gone */
    ASSERT_EQ(0x1234, reply_word_at(0x76));

    reset_mocks();
    fill_mem_rec();
    MMAP_$REAL_PAGES = 0x10000;
    run_local(0x06, NULL);
    ASSERT_EQ(0x0000, reply_word_at(0x76));
}

/*
 * 0x00E648CC-0x00E648D2 and 0x00E64900-0x00E64906: the node-uid arm writes
 * -(uid.high) at reply+0x12 and 1-(uid.high) at reply+0x22, the two
 * complements the remote arm checks at 0x00E6581C and 0x00E657F6.
 */
TEST(node_uid_reply_carries_the_uid_complements)
{
    uint32_t at_12, at_22;

    mock_node_uid.high = 0x11223344;
    mock_node_uid.low  = 0x55667788;

    run_local(0x04, NULL);

    ASSERT_EQ(0x11223344u, reply[2]);
    ASSERT_EQ(0x55667788u, reply[3]);
    memcpy(&at_12, reply_bytes + 0x12, 4);
    memcpy(&at_22, reply_bytes + 0x22, 4);
    ASSERT_EQ((uint32_t)-0x11223344, at_12);
    ASSERT_EQ((uint32_t)(1 - 0x11223344), at_22);
    /* the two checks the remote arm makes */
    ASSERT_EQ(0u, at_12 + reply[2]);
    ASSERT_EQ(1u, at_22 + reply[2]);
}

/*
 * 0x00E64786 -> 0x00E65502: an unrecognised request type puts 0x0011000D in
 * the caller's status_ret, and reply+0x04 keeps the (zero) local status that
 * 0x00E65516 copies there.
 */
TEST(unknown_request_writes_status_ret_not_the_reply_status)
{
    status_$t status;

    status = run_local(0x07, NULL);

    ASSERT_EQ(status_$network_unknown_request_type, status);
    ASSERT_EQ(0u, reply[1]);
    /* 0x00E65508-0x00E65512: the reply still gets request+1 at +0x02 */
    ASSERT_EQ(0x08, reply_word_at(0x02));
}

int main(void)
{
    printf("ASKNODE_$INTERNET_INFO tests\n");
    RUN_TEST(mem_rec_is_the_0x56_bytes_the_loop_moves);
    RUN_TEST(stats_copies_whole_mem_rec_to_reply_0x72);
    RUN_TEST(stats_mem_rec_copy_does_not_overrun);
    RUN_TEST(stats_reply_carries_the_page_error_table);
    RUN_TEST(stats_real_pages_word_follows_the_copy);
    RUN_TEST(node_uid_reply_carries_the_uid_complements);
    RUN_TEST(unknown_request_writes_status_ret_not_the_reply_status);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
