/*
 * disk/test/test_read_multi.c - unit tests for DISK_$READ_MULTI
 * (0x00E3CFCC)
 */

#include "disk/disk_internal.h"
#include "netlog/netlog.h"
#include "proc1/proc1.h"
#include "mmu/mmu.h"

#include <stdio.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;

static void reset_state(void);

#define RUN_TEST(name) do { \
    printf("  Running %s... ", #name); \
    reset_state(); \
    test_##name(); \
    printf("PASSED\n"); \
    tests_passed++; \
} while (0)

#define ASSERT_EQ(expected, actual) do { \
    if ((unsigned long)(expected) != (unsigned long)(actual)) { \
        printf("FAILED\n    Expected: 0x%lx, Got: 0x%lx at line %d\n", \
               (unsigned long)(expected), (unsigned long)(actual), __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

/* ---- data ------------------------------------------------------------ */

uint8_t DISK_$DATA[DISK_$DATA_SIZE] __attribute__((aligned(16)));
uint16_t PROC1_$CURRENT;
int8_t NETLOG_$OK_TO_LOG;

static uint8_t arena[0x400] __attribute__((aligned(16)));
static uint16_t dev_word[8];            /* dev_info; the flags are word 4 */

/* ---- mocks ----------------------------------------------------------- */

static int n_io, n_map, n_do_io, n_wait, n_err, n_mcr;
static uint16_t io_op;
static status_$t io_fail_at_call;           /* status for the Nth DISK_IO */
static int io_fail_n;
static status_$t map_status;
static int8_t do_io_queued;
static uint16_t wait_mask;
static status_$t err_result;
static int16_t err_vol;
static int good_hdr;                        /* DO_IO writes correct headers */

status_$t DISK_IO(uint16_t op, uint16_t vol_idx, uint32_t ppn, uint32_t daddr,
                  uint32_t *info)
{
    (void)vol_idx; (void)ppn; (void)daddr; (void)info;
    io_op = op;
    n_io++;
    return (n_io == io_fail_n) ? io_fail_at_call : 0;
}

void disk_$map_request(disk_io_req_t *req, int16_t vol_idx, int16_t op,
                       disk_$vol_map_entry_t *map, status_$t *status)
{
    disk_io_req_t *r;
    (void)op;
    n_map++;
    /* relink the +0x08 chain through +0x00 for the volume */
    for (r = req; r != NULL; r = (disk_io_req_t *)ARCH_VA_TO_PTR(r->free_next)) {
        r->next = r->free_next;
        map[vol_idx - 1].tail = r;
    }
    map[vol_idx - 1].head = req;
    *status = map_status;
}

void DISK_$DO_IO(void *dev, void *req, void *tail, void *result)
{
    disk_io_req_t *r = (disk_io_req_t *)req;
    uint32_t page = r->header[2] ^ 0xFFFFFFFFu;
    uint32_t h0 = r->header[0], h1 = r->header[1];
    (void)dev; (void)tail;
    n_do_io++;
    for (; r != NULL; r = (disk_io_req_t *)ARCH_VA_TO_PTR(r->next)) {
        if (good_hdr) {                     /* the blocks' own headers */
            r->header[0] = h0; r->header[1] = h1; r->header[2] = page++;
        }
    }
    *(int8_t *)result = do_io_queued;
}

void (MMU_$MCR_CHANGE)(uint32_t bit_slot) { uint16_t bit = (uint16_t)ARCH_PASCAL_SLOT_WORD(bit_slot); (void)bit; (void)bit; n_mcr++; }

void disk_$wait_io(uint16_t mask, int32_t *io_wait, int32_t *err_wait)
{
    (void)io_wait; (void)err_wait;
    n_wait++; wait_mask = mask;
}

void disk_$io_error(int16_t vol_idx, disk_io_req_t *req, uint32_t *info)
{
    (void)info;
    n_err++; err_vol = vol_idx;
    req->status = err_result;
    err_result = 0x00080009;                /* a second call fails for good */
}

#include "../read_multi.c"

/* ---- helpers --------------------------------------------------------- */

static disk_io_req_t *blk(int i) { return (disk_io_req_t *)(void *)&arena[i * 0x40]; }
static uint32_t va(int i) { return 0x10000u + (uint32_t)i * 0x40u; }

static void reset_state(void)
{
    int i;
    memset(DISK_$DATA, 0, sizeof(DISK_$DATA));
    memset(arena, 0, sizeof(arena));
    memset(dev_word, 0, sizeof(dev_word));
    ARCH_HOST_VA_BASE = (uintptr_t)arena - 0x10000u;
    PROC1_$CURRENT = 1;
    NETLOG_$OK_TO_LOG = 0;
    n_io = n_map = n_do_io = n_wait = n_err = n_mcr = 0;
    io_fail_n = 0; io_fail_at_call = 0; map_status = 0;
    do_io_queued = 0; err_result = 0; good_hdr = 1;
    for (i = 0; i < 3; i++) {
        blk(i)->free_next = (i < 2) ? va(i + 1) : 0;
    }
    blk(0)->header[0] = 0xAAAA; blk(0)->header[1] = 0xBBBB; blk(0)->header[2] = 5;
    DISK_VOL(3)->mount_state = DISK_MOUNT_BUSY;
    DISK_VOL(3)->dev_info = dev_word;
}

/* ---- tests ----------------------------------------------------------- */

static void test_bad_volume_index(void)
{
    int16_t pages = 7;
    status_$t st;
    DISK_$READ_MULTI(11, -1, -1, va(0), va(2), &pages, &st);
    ASSERT_EQ(0x0008000F, st);
    ASSERT_EQ(0, pages);
}

static void test_mount_checks(void)
{
    int16_t pages;
    status_$t st;
    DISK_VOL(3)->mount_state = DISK_MOUNT_ASSIGNED;
    DISK_$READ_MULTI(3, -1, -1, va(0), va(2), &pages, &st);
    ASSERT_EQ(0x0008000D, st);
    /* FALSE: state 2 alone is enough */
    NETLOG_$OK_TO_LOG = (int8_t)0xFF;
    DISK_$READ_MULTI(3, 0, -1, va(0), va(2), &pages, &st);
    ASSERT_EQ(0, st);
    /* FALSE: or ownership alone */
    DISK_VOL(3)->mount_state = DISK_MOUNT_RESERVED;
    DISK_VOL(3)->mount_proc = 1;
    DISK_$READ_MULTI(3, 0, -1, va(0), va(2), &pages, &st);
    ASSERT_EQ(0, st);
}

static void test_sync_reads_with_expected_headers(void)
{
    int16_t pages;
    status_$t st;
    NETLOG_$OK_TO_LOG = (int8_t)0xFF;
    DISK_$READ_MULTI(3, -1, -1, va(0), va(2), &pages, &st);
    ASSERT_EQ(3, pages);
    ASSERT_EQ(3, n_io);
    ASSERT_EQ(0, io_op);
    ASSERT_EQ(6, blk(1)->header[2]);
    ASSERT_EQ(7, blk(2)->header[2]);
    ASSERT_EQ(0xAAAA, blk(2)->header[0]);
}

static void test_sync_stops_at_failure(void)
{
    int16_t pages;
    status_$t st;
    NETLOG_$OK_TO_LOG = (int8_t)0xFF;
    io_fail_n = 2; io_fail_at_call = 0x00080009;
    DISK_$READ_MULTI(3, -1, 0, va(0), va(2), &pages, &st);
    ASSERT_EQ(2, io_op);
    ASSERT_EQ(1, pages);
    ASSERT_EQ(2, n_io);
    ASSERT_EQ(0, st);                           /* the FIRST block's status */
}

static void test_queued_read_ok(void)
{
    int16_t pages;
    status_$t st;
    dev_word[4] = 0x2000;
    do_io_queued = (int8_t)0xFF;
    DISK_$READ_MULTI(3, -1, -1, va(0), va(2), &pages, &st);
    ASSERT_EQ(1, n_map);
    ASSERT_EQ(1, n_do_io);
    ASSERT_EQ(1, n_mcr);
    ASSERT_EQ(1, n_wait);
    ASSERT_EQ(0x0008, wait_mask);
    ASSERT_EQ(3, pages);
    ASSERT_EQ(0, st);
    ASSERT_EQ(0xFF, (uint8_t)DISK_$PER_PROC[1].io_pending);
}

static void test_queued_bad_header_recovered_then_fails(void)
{
    int16_t pages;
    status_$t st;
    dev_word[4] = 0x2000;
    good_hdr = 0;                               /* headers stay inverted */
    err_result = status_$disk_ok_after_retry;
    DISK_$READ_MULTI(3, -1, -1, va(0), va(2), &pages, &st);
    ASSERT_EQ(2, n_err);                        /* recovered, re-checked, fails */
    ASSERT_EQ(3, err_vol);
    ASSERT_EQ(0x00080009, st);
    ASSERT_EQ(0, pages);
}

static void test_queued_no_verify_device(void)
{
    int16_t pages;
    status_$t st;
    dev_word[4] = 0xA000;                       /* bit 15: no verify */
    good_hdr = 0;
    DISK_$READ_MULTI(3, -1, -1, va(0), va(2), &pages, &st);
    ASSERT_EQ(0, n_err);
    ASSERT_EQ(3, pages);
}

static void test_queued_map_failure(void)
{
    int16_t pages;
    status_$t st;
    dev_word[4] = 0x2000;
    map_status = 0x00080012;
    blk(0)->status = 0x55;
    DISK_$READ_MULTI(3, -1, -1, va(0), va(2), &pages, &st);
    ASSERT_EQ(0x00080012, st);                  /* not the block's status */
    ASSERT_EQ(0, n_do_io);
    ASSERT_EQ(0, (uint8_t)DISK_$PER_PROC[1].io_pending);
}

int main(void)
{
    printf("DISK_$READ_MULTI tests:\n");
    RUN_TEST(bad_volume_index);
    RUN_TEST(mount_checks);
    RUN_TEST(sync_reads_with_expected_headers);
    RUN_TEST(sync_stops_at_failure);
    RUN_TEST(queued_read_ok);
    RUN_TEST(queued_bad_header_recovered_then_fails);
    RUN_TEST(queued_no_verify_device);
    RUN_TEST(queued_map_failure);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
