/*
 * disk/test/test_io.c - Unit tests for DISK_IO (0x00e3d50e)
 *
 * The real disk/io.c is #included below and driven through mocks for
 * disk_$map_request, DISK_$DO_IO, disk_$wait_io, the queue-block allocator,
 * disk_$chksum_page, disk_$io_error, NETLOG and ML.
 *
 * The tests pin down the things the previous (unbuilt) reconstruction got
 * wrong:
 *   - argument 3 is the physical page number and argument 4 the disk address
 *     (0xe3d658 / 0xe3d606), the order every real caller uses
 *   - a format request rewrites the daddr longword into (0, head, 0)
 *     (0xe3d630-0xe3d63a)
 *   - the transfer runs against the first volume disk_$map_request marked in
 *     the 10-entry map (0xe3d63e)
 *   - reads travel with the block number complemented (0xe3d6c8) and the
 *     header is handed back to the caller afterwards (0xe3d91c)
 *   - a write-protected volume is remembered in the descriptor (0xe3d75c)
 */

#include "disk/disk_internal.h"

#include <stdio.h>
#include <string.h>

/* ================================================================
 * Test harness
 * ================================================================ */

static int tests_failed = 0;
static int tests_run = 0;

#define RUN_TEST(name) do {                     \
    tests_run++;                                \
    printf("  Running %s... ", #name);          \
    fflush(stdout);                             \
    if (test_##name() == 0) printf("PASSED\n"); \
} while (0)

#define CHECK(cond) do {                                            \
    if (!(cond)) {                                                  \
        printf("FAILED\n    %s at %s:%d\n", #cond, __FILE__,        \
               __LINE__);                                           \
        tests_failed++;                                             \
        return 1;                                                   \
    }                                                               \
} while (0)

#define CHECK_EQ(expected, actual) do {                             \
    unsigned long e_ = (unsigned long)(expected);                   \
    unsigned long a_ = (unsigned long)(actual);                     \
    if (e_ != a_) {                                                 \
        printf("FAILED\n    %s: expected 0x%lx, got 0x%lx at %s:%d\n", \
               #actual, e_, a_, __FILE__, __LINE__);                \
        tests_failed++;                                             \
        return 1;                                                   \
    }                                                               \
} while (0)

/* ================================================================
 * Globals the implementation links against
 * ================================================================ */

uint16_t PROC1_$CURRENT = 2;
uint32_t TIME_$CLOCKH = 0x11223344;
int8_t NETLOG_$OK_TO_LOG = 0;

/* Volume table: DISK_VOLUME_BASE is a fixed address on m68k, so the host
 * build points it at this array instead (see the override below). */
static uint8_t mock_disk_data[0xB00];
#undef DISK_VOLUME_BASE
#define DISK_VOLUME_BASE (mock_disk_data)

ml_$exclusion_t ml_$exclusion_t_00e7a274;
MODULE_DATA_DEFINE(pmap_$data_t, PMAP_$DATA, 0x00E24D44);

/* ================================================================
 * Mocks
 * ================================================================ */

/*
 * disk_$get_qblks_internal fills in two four-byte VA cells (0x00E3BF7E
 * `move.l (0xc0,A5),(A0)` and 0x00E3BFB8 `move.l (0xc0,A5),(A2)`), so the
 * mock has to hand back target VAs, not host pointers.  The request block
 * therefore lives in an arena that ARCH_HOST_VA_BASE points at, and the
 * cells hold offsets into it.  The two VA cells are adjacent in the frame
 * ((-0x90,A6) and (-0x8c,A6)), so a mock that wrote a host pointer would
 * corrupt the second one on a 64-bit host.
 */
#define MOCK_REQ_VA     0x40u
#define MOCK_REQ_LAST_VA 0xC0u

static uint8_t qblk_arena[0x140];
static disk_io_req_t *const mock_req_p =
    (disk_io_req_t *)(qblk_arena + MOCK_REQ_VA);

static int qblk_get_count;
static int qblk_rtn_count;
static int8_t qblk_get_mode;
static uint32_t qblk_first_out;
static uint32_t qblk_last_out;

void disk_$get_qblks_internal(int16_t count, int8_t mode, uint32_t *first_out,
                              uint32_t *last_out)
{
    (void)count;
    qblk_get_mode = mode;
    qblk_get_count++;
    memset(mock_req_p, 0, sizeof(*mock_req_p));
    *first_out = MOCK_REQ_VA;
    *last_out = MOCK_REQ_LAST_VA;
    qblk_first_out = *first_out;
    qblk_last_out = *last_out;
}

void disk_$rtn_qblks_internal(int16_t count, void *blocks, void *param_3)
{
    (void)count; (void)blocks; (void)param_3;
    qblk_rtn_count++;
}

/* disk_$map_request: marks map entry `map_mark_volx` and reports map_status */
static int16_t map_mark_volx;
static status_$t map_status;
static int16_t map_seen_op;
static int map_count;

void disk_$map_request(disk_io_req_t *req, int16_t vol_idx, int16_t internal_op,
                       disk_$vol_map_entry_t *volume_map, status_$t *status)
{
    (void)vol_idx;
    map_count++;
    map_seen_op = internal_op;
    if (map_mark_volx > 0) {
        volume_map[map_mark_volx - 1].head = req;
        volume_map[map_mark_volx - 1].tail = req;
    }
    *status = map_status;
}

static int io_error_count;
static int16_t io_error_volx;

void disk_$io_error(int16_t vol_idx, disk_io_req_t *req, uint32_t *info)
{
    (void)req; (void)info;
    io_error_count++;
    io_error_volx = vol_idx;
}

static uint16_t chksum_result;
static int chksum_count;

uint16_t disk_$chksum_page(uint32_t *ppn)
{
    (void)ppn;
    chksum_count++;
    return chksum_result;
}

/* DISK_$DO_IO: records the descriptor it was handed and sets the request's
 * status and the "queued" byte from the fixtures. */
static void *do_io_desc;
static disk_io_req_t *do_io_req;
static status_$t do_io_status;
static char do_io_queued;
static int do_io_count;
/* header the "device" returns to the caller */
static uint32_t do_io_reply[8];
static int do_io_reply_valid;

void DISK_$DO_IO(void *dev_entry, void *req, void *param_3, void *result)
{
    (void)param_3;
    do_io_count++;
    do_io_desc = dev_entry;
    do_io_req = (disk_io_req_t *)req;
    if (do_io_reply_valid) {
        memcpy(do_io_req->header, do_io_reply, sizeof(do_io_reply));
    }
    do_io_req->status = do_io_status;
    *(char *)result = do_io_queued;
}

static int wait_io_count;
static uint16_t wait_io_mask;

void disk_$wait_io(uint16_t disk_mask, int32_t *io_wait_val,
                   int32_t *error_wait_val)
{
    (void)io_wait_val; (void)error_wait_val;
    wait_io_count++;
    wait_io_mask = disk_mask;
}

static int excl_start_count, excl_stop_count;
void ML_$EXCLUSION_START(ml_$exclusion_t *l) { (void)l; excl_start_count++; }
void ML_$EXCLUSION_STOP(ml_$exclusion_t *l)  { (void)l; excl_stop_count++; }

static int mcr_change_count;
void (MMU_$MCR_CHANGE)(uint32_t bit_slot) { uint16_t bit = (uint16_t)ARCH_PASCAL_SLOT_WORD(bit_slot); (void)bit; (void)bit; mcr_change_count++; }

static int netlog_count;
void NETLOG_$LOG_IT(uint16_t kind, uint32_t *uid, uint16_t p3, uint16_t p4,
                    uint16_t p5, uint16_t p6, uint16_t p7, uint16_t p8)
{
    (void)kind; (void)uid; (void)p3; (void)p4;
    (void)p5; (void)p6; (void)p7; (void)p8;
    netlog_count++;
}

static int crash_count;
static status_$t crash_status;
void CRASH_SYSTEM(const status_$t *status_p)
{
    crash_count++;
    crash_status = *status_p;
}

/* ================================================================
 * Code under test
 * ================================================================ */

#include "../io.c"

/* DISK_$DIAG, DISK_$DO_CHKSUM and the module exclusion lock are cells of the
 * DISK_ module block (disk/disk.h), so the host build provides the block. */
uint8_t DISK_$DATA[DISK_$DATA_SIZE];

/* ================================================================
 * Fixtures
 * ================================================================ */

#define TEST_VOL    3

/* dev_info: the flag word DISK_IO reads lives at +8 */
static uint16_t mock_dev_info[8];

static void reset_all(uint16_t dev_flags)
{
    /* The queue-block VA cells hold offsets into qblk_arena. */
    ARCH_HOST_VA_BASE = (uintptr_t)qblk_arena;

    memset(qblk_arena, 0, sizeof(qblk_arena));
    memset(mock_disk_data, 0, sizeof(mock_disk_data));
    memset(mock_dev_info, 0, sizeof(mock_dev_info));
    memset(mock_req_p, 0, sizeof(*mock_req_p));
    memset(do_io_reply, 0, sizeof(do_io_reply));

    qblk_get_count = qblk_rtn_count = 0;
    qblk_get_mode = 0;
    map_mark_volx = TEST_VOL;
    map_status = status_$ok;
    map_count = 0;
    map_seen_op = -1;
    io_error_count = 0;
    chksum_count = 0;
    chksum_result = 0;
    do_io_count = 0;
    do_io_desc = NULL;
    do_io_req = NULL;
    do_io_status = status_$ok;
    do_io_queued = 0;
    do_io_reply_valid = 0;
    wait_io_count = 0;
    excl_start_count = excl_stop_count = 0;
    mcr_change_count = 0;
    netlog_count = 0;
    crash_count = 0;
    NETLOG_$OK_TO_LOG = 0;

    mock_dev_info[4] = dev_flags;               /* word at dev_info+8 */
    DISK_VOL(TEST_VOL)->dev_info = mock_dev_info;
}

/* ================================================================
 * Tests
 * ================================================================ */

/* 0xe3d528: bls -> an unsigned compare against 10 */
static int test_rejects_out_of_range_volume(void)
{
    reset_all(0x8000);
    CHECK_EQ(status_$invalid_volume_index, DISK_IO(0, 11, 0x100, 0x200, NULL));
    CHECK_EQ(0, qblk_get_count);
    return 0;
}

/* 0xe3d584: op 1 on a write-protected volume fails before allocating */
static int test_write_protect_rejects_write(void)
{
    reset_all(0x8000);
    DISK_VOL(TEST_VOL)->as_options |= DISK_VOL_FLAG_WRITE_PROTECT;

    CHECK_EQ(status_$disk_write_protected,
             DISK_IO(1, TEST_VOL, 0x100, 0x200, NULL));
    CHECK_EQ(0, qblk_get_count);
    CHECK_EQ(0, map_count);
    return 0;
}

/*
 * A plain read: arg 3 is the ppn and arg 4 the disk address; the block number
 * is complemented on the way out (0xe3d6c8) and the device's header is copied
 * back to the caller (0xe3d91c).
 */
static int test_read_arg_order_and_header_writeback(void)
{
    uint32_t info[8];
    status_$t st;
    int i;

    reset_all(0x8000);                  /* flag word < 0 -> no header check */
    for (i = 0; i < 8; i++) {
        info[i] = 0x1000u + i;
    }
    do_io_reply_valid = 1;
    for (i = 0; i < 8; i++) {
        do_io_reply[i] = 0x2000u + i;
    }

    st = DISK_IO(0, TEST_VOL, 0xABCD, 0x1234, info);

    CHECK_EQ(status_$ok, st);
    CHECK_EQ(1, qblk_get_count);
    /* 0xe3d5e4: st -(SP) -> write mode */
    CHECK_EQ((int8_t)0xFF, qblk_get_mode);
    CHECK_EQ(DISK_INTERNAL_OP_READ, map_seen_op);
    /* arg 3 = ppn (0xe3d658), arg 4 = daddr (0xe3d606) */
    CHECK_EQ(0xABCDu, mock_req_p->ppn);
    CHECK_EQ(1, do_io_count);
    CHECK_EQ(1, mcr_change_count);
    /* header handed back for reads */
    for (i = 0; i < 8; i++) {
        CHECK_EQ(0x2000u + i, info[i]);
    }
    CHECK_EQ(1, qblk_rtn_count);
    return 0;
}

/* 0xe3d6c8: not.l (0x28,A3) - the read carries a complemented block number */
static int test_read_complements_block_number(void)
{
    uint32_t info[8];

    reset_all(0x8000);
    memset(info, 0, sizeof(info));
    info[2] = 0x00001357u;
    do_io_reply_valid = 0;

    DISK_IO(0, TEST_VOL, 0x10, 0x20, info);

    CHECK_EQ(~0x00001357u, mock_req_p->header[2]);
    return 0;
}

/* 0xe3d630-0xe3d63a: a format request rewrites the daddr longword */
static int test_format_rewrites_daddr(void)
{
    uint32_t info[8];

    reset_all(0x8000);
    memset(info, 0, sizeof(info));

    DISK_IO(4, TEST_VOL, 0x40, 0x00123456u, info);

    CHECK_EQ(DISK_INTERNAL_OP_FORMAT, map_seen_op);
    /* head = low byte of daddr at +0x06, sector +0x07 = 0, high word = 0 */
    CHECK_EQ(0x00005600u, mock_req_p->daddr);
    CHECK_EQ(0x56, disk_req_head(mock_req_p));
    CHECK_EQ(0, disk_req_sector(mock_req_p));
    CHECK_EQ(0, disk_req_daddr_hi(mock_req_p));
    return 0;
}

/*
 * 0xe3d63e: the transfer is issued against the first volume the map marks,
 * and 0xe3d730/0xe3d742 wait on that volume's bit when the driver queues it.
 */
static int test_uses_first_mapped_volume(void)
{
    uint32_t info[8];

    reset_all(0x8000);
    memset(info, 0, sizeof(info));
    map_mark_volx = 5;
    do_io_queued = (char)0xFF;

    DISK_IO(0, TEST_VOL, 0x10, 0x20, info);

    CHECK(do_io_desc == (void *)DISK_VOL(5));
    CHECK_EQ(1, wait_io_count);
    CHECK_EQ(1u << 5, wait_io_mask);
    return 0;
}

/* 0xe3d75c: a write-protected reply is latched into the descriptor */
static int test_write_protect_reply_latched(void)
{
    uint32_t info[8];
    status_$t st;

    reset_all(0x8000);
    memset(info, 0, sizeof(info));
    do_io_status = status_$disk_write_protected;

    st = DISK_IO(0, TEST_VOL, 0x10, 0x20, info);

    CHECK_EQ(status_$disk_write_protected, st);
    CHECK((DISK_VOL(TEST_VOL)->as_options & DISK_VOL_FLAG_WRITE_PROTECT) != 0);
    CHECK_EQ(0, io_error_count);        /* the WP path skips disk_$io_error */
    CHECK_EQ(1, qblk_rtn_count);
    return 0;
}

/* 0xe3d778-0xe3d796: three driver statuses are swallowed and retried past */
static int test_recoverable_error_cleared(void)
{
    uint32_t info[8];
    status_$t st;

    reset_all(0x8000);
    memset(info, 0, sizeof(info));
    do_io_status = status_$disk_ok_after_retry;

    st = DISK_IO(0, TEST_VOL, 0x10, 0x20, info);

    CHECK_EQ(status_$ok, st);
    CHECK_EQ(1, io_error_count);
    CHECK_EQ(TEST_VOL, io_error_volx);
    return 0;
}

/*
 * 0xe3d7a4-0xe3d7c4: with header checking on (device flag word >= 0 and op
 * not 2 or 4) a mismatched header is reported as 0x80011.
 */
static int test_header_mismatch_detected(void)
{
    uint32_t info[8];
    status_$t st;
    int i;

    reset_all(0x0000);                  /* flag word >= 0 -> header check on */
    for (i = 0; i < 8; i++) {
        info[i] = 0x3000u + i;
    }
    do_io_reply_valid = 1;
    for (i = 0; i < 8; i++) {
        do_io_reply[i] = 0x3000u + i;
    }
    do_io_reply[1] = 0xDEADBEEFu;       /* UID low differs */

    st = DISK_IO(0, TEST_VOL, 0x10, 0x20, info);

    CHECK_EQ(status_$disk_block_header_error, st);
    CHECK_EQ(1, io_error_count);
    return 0;
}

/*
 * A matching header passes.  DISK_IO complements info[2] into the request
 * before the transfer, but the driver overwrites the whole header with what
 * it read, and 0xe3d7be compares that against the caller's info[2] as-is.
 */
static int test_header_match_passes(void)
{
    uint32_t info[8];
    status_$t st;
    int i;

    reset_all(0x0000);
    for (i = 0; i < 8; i++) {
        info[i] = 0x4000u + i;
    }
    do_io_reply_valid = 1;
    for (i = 0; i < 8; i++) {
        do_io_reply[i] = 0x4000u + i;
    }
    st = DISK_IO(0, TEST_VOL, 0x10, 0x20, info);

    CHECK_EQ(status_$ok, st);
    CHECK_EQ(0, io_error_count);
    CHECK_EQ(0, crash_count);
    return 0;
}

/*
 * 0xe3d674 / 0xe3d90a: a checksummed device takes the module's second
 * exclusion lock for the whole transfer, and 0xe3d688 stamps the write.
 */
static int test_checksummed_write_stamps_and_locks(void)
{
    uint32_t info[8];

    reset_all(DEV_FLAG_CHECKSUM);       /* flag word >= 0 and bit 14 set */
    memset(info, 0, sizeof(info));
    chksum_result = 0xBEEF;

    DISK_IO(1, TEST_VOL, 0x10, 0x20, info);

    CHECK_EQ(1, excl_start_count);
    CHECK_EQ(1, excl_stop_count);
    CHECK_EQ(DISK_INTERNAL_OP_WRITE, map_seen_op);
    CHECK_EQ(TIME_$CLOCKH, mock_req_p->header[3]);
    CHECK_EQ(0xBEEF, disk_req_chksum(mock_req_p));
    /* the read-after-write pass is skipped while the scratch page is 0 */
    CHECK_EQ(1, do_io_count);
    return 0;
}

/*
 * 0xe3d59a-0xe3d5b2: a raw write (op 3) skips the timestamp/checksum stamp
 * even on a checksummed device.
 */
static int test_raw_write_skips_stamp(void)
{
    uint32_t info[8];

    reset_all(DEV_FLAG_CHECKSUM);
    memset(info, 0, sizeof(info));
    chksum_result = 0xBEEF;

    DISK_IO(3, TEST_VOL, 0x10, 0x20, info);

    CHECK_EQ(0u, mock_req_p->header[3]);
    CHECK_EQ(0, disk_req_chksum(mock_req_p));
    CHECK_EQ(1, excl_start_count);      /* the lock is still taken */
    return 0;
}

/* 0xe3d88e: nothing is logged while NETLOG_$OK_TO_LOG is clear */
static int test_netlog_gate(void)
{
    uint32_t info[8];

    reset_all(0x8000);
    memset(info, 0, sizeof(info));
    DISK_IO(0, TEST_VOL, 0x10, 0x20, info);
    CHECK_EQ(0, netlog_count);

    reset_all(0x8000);
    memset(info, 0, sizeof(info));
    NETLOG_$OK_TO_LOG = (int8_t)0xFF;
    DISK_IO(0, TEST_VOL, 0x10, 0x20, info);
    CHECK_EQ(1, netlog_count);
    return 0;
}

int main(void)
{
    printf("=== DISK_IO tests ===\n");

    RUN_TEST(rejects_out_of_range_volume);
    RUN_TEST(write_protect_rejects_write);
    RUN_TEST(read_arg_order_and_header_writeback);
    RUN_TEST(read_complements_block_number);
    RUN_TEST(format_rewrites_daddr);
    RUN_TEST(uses_first_mapped_volume);
    RUN_TEST(write_protect_reply_latched);
    RUN_TEST(recoverable_error_cleared);
    RUN_TEST(header_mismatch_detected);
    RUN_TEST(header_match_passes);
    RUN_TEST(checksummed_write_stamps_and_locks);
    RUN_TEST(raw_write_skips_stamp);
    RUN_TEST(netlog_gate);

    printf("\n%d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
