/*
 * disk/test/test_write_multi.c - unit tests for DISK_$WRITE_MULTI
 * (0x00E3CCEE) and disk_$set_chain_status (0x00E3CCCE)
 */

#include "disk/disk_internal.h"
#include "netlog/netlog.h"
#include "proc1/proc1.h"

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

static int n_io, n_map, n_do_io, n_wait, n_err;
static uint16_t io_vol[8];
static status_$t io_result;
static status_$t map_status;
static int8_t do_io_queued;
static uint16_t wait_mask;
static int32_t wait_io_value, wait_err_value;
static uint16_t err_vol;
static status_$t err_result;
static status_$t do_io_sets[4];

status_$t DISK_IO(uint16_t op, uint16_t vol_idx, uint32_t ppn, uint32_t daddr,
                  uint32_t *info)
{
    (void)op; (void)ppn; (void)daddr; (void)info;
    io_vol[n_io++ & 7] = vol_idx;
    return io_result;
}

void disk_$map_request(disk_io_req_t *req, int16_t vol_idx, int16_t op,
                       disk_$vol_map_entry_t *map, status_$t *status)
{
    disk_io_req_t *t = req;
    (void)op;
    n_map++;
    while (t->next != 0) t = (disk_io_req_t *)ARCH_VA_TO_PTR(t->next);
    map[vol_idx - 1].head = req;
    map[vol_idx - 1].tail = t;
    *status = map_status;
}

void DISK_$DO_IO(void *dev, void *req, void *tail, void *result)
{
    disk_io_req_t *r = (disk_io_req_t *)req;
    int i = 0;
    (void)dev; (void)tail;
    n_do_io++;
    for (; r != NULL; r = (disk_io_req_t *)ARCH_VA_TO_PTR(r->next)) {
        r->status = do_io_sets[i++ & 3];
    }
    *(int8_t *)result = do_io_queued;
    DISK_$PER_PROC[PROC1_$CURRENT].io_pending = 0;     /* retired */
}

void disk_$wait_io(uint16_t mask, int32_t *io_wait, int32_t *err_wait)
{
    n_wait++; wait_mask = mask; wait_io_value = *io_wait; wait_err_value = *err_wait;
}

void disk_$io_error(int16_t vol_idx, disk_io_req_t *req, uint32_t *info)
{
    (void)info;
    n_err++; err_vol = (uint16_t)vol_idx;
    req->status = err_result;
}

#include "../write_multi.c"

/* ---- helpers --------------------------------------------------------- */

static disk_io_req_t *blk(int i) { return (disk_io_req_t *)(void *)&arena[i * 0x40]; }
static uint32_t va(int i) { return 0x10000u + (uint32_t)i * 0x40u; }

static void setup_vol(int v, uint16_t state, int16_t proc)
{
    DISK_VOL(v)->mount_state = state;
    DISK_VOL(v)->mount_proc = proc;
    DISK_VOL(v)->dev_info = dev_word;
    DISK_VOL(v)->as_options = 0;
}

static void reset_state(void)
{
    memset(DISK_$DATA, 0, sizeof(DISK_$DATA));
    memset(arena, 0, sizeof(arena));
    memset(dev_word, 0, sizeof(dev_word));
    memset(do_io_sets, 0, sizeof(do_io_sets));
    ARCH_HOST_VA_BASE = (uintptr_t)arena - 0x10000u;
    PROC1_$CURRENT = 1;
    NETLOG_$OK_TO_LOG = 0;
    n_io = n_map = n_do_io = n_wait = n_err = 0;
    io_result = 0; map_status = 0; do_io_queued = 0; err_result = 0;
    /* blocks 0,1 on volume 1; blocks 2,3 on volume 2 */
    blk(0)->next = va(1); blk(0)->op_flags = 1;
    blk(1)->next = va(2); blk(1)->op_flags = 1;
    blk(2)->next = va(3); blk(2)->op_flags = 2;
    blk(3)->next = 0;     blk(3)->op_flags = 2;
    setup_vol(1, DISK_MOUNT_BUSY, 0);
    setup_vol(2, DISK_MOUNT_BUSY, 0);
}

/* ---- tests ----------------------------------------------------------- */

static void test_set_chain_status(void)
{
    const status_$t st = 0x1234;
    disk_$set_chain_status(blk(0), &st);
    ASSERT_EQ(0x1234, blk(0)->status);
    ASSERT_EQ(0x1234, blk(3)->status);
}

static void test_sync_runs_and_mount_failure(void)
{
    status_$t status = 99;
    NETLOG_$OK_TO_LOG = (int8_t)0xFF;           /* synchronous path */
    setup_vol(2, DISK_MOUNT_RESERVED, 1);
    io_result = 0x77;
    DISK_$WRITE_MULTI((int8_t)0xFF, blk(0), &status);
    ASSERT_EQ(0, status);
    ASSERT_EQ(2, n_io);
    ASSERT_EQ(1, io_vol[0]);
    ASSERT_EQ(0x77, blk(0)->status);
    ASSERT_EQ(0x77, blk(1)->status);
    ASSERT_EQ(0, blk(1)->next);                 /* the run was cut off */
    ASSERT_EQ(0x0008000D, blk(2)->status);
    ASSERT_EQ(0x0008000D, blk(3)->status);
    ASSERT_EQ(0, n_do_io);
    ASSERT_EQ(0xFF, (uint8_t)DISK_$PER_PROC[1].io_pending);
}

static void test_owner_mode_needs_state_2_and_owner(void)
{
    status_$t status;
    NETLOG_$OK_TO_LOG = (int8_t)0xFF;
    setup_vol(1, DISK_MOUNT_ASSIGNED, 1);       /* ok */
    setup_vol(2, DISK_MOUNT_ASSIGNED, 5);       /* someone else's */
    DISK_$WRITE_MULTI(0, blk(0), &status);
    ASSERT_EQ(2, n_io);
    ASSERT_EQ(0x0008000D, blk(2)->status);
}

static void test_queued_and_postprocessed(void)
{
    status_$t status;
    dev_word[4] = 0x0800;                       /* queued writes */
    DISK_$PER_PROC[1].io_ec.value = 10;
    DISK_$PER_PROC[1].err_ec.value = 20;
    do_io_queued = (int8_t)0xFF;
    do_io_sets[0] = status_$disk_write_protected;
    do_io_sets[1] = 0x00080009;
    err_result = status_$disk_ok_after_retry;
    DISK_$WRITE_MULTI((int8_t)0xFF, blk(0), &status);
    ASSERT_EQ(0, n_io);
    ASSERT_EQ(2, n_map);
    ASSERT_EQ(2, n_do_io);
    ASSERT_EQ(1, n_wait);
    ASSERT_EQ(0x0006, wait_mask);               /* volumes 1 and 2 */
    ASSERT_EQ(12, wait_io_value);
    ASSERT_EQ(21, wait_err_value);
    ASSERT_EQ(DISK_VOL_FLAG_WRITE_PROTECT, DISK_VOL(1)->as_options & 1);
    ASSERT_EQ(2, n_err);                        /* blk 1 and blk 3 */
    ASSERT_EQ(0, blk(1)->status);               /* recovered -> ok */
}

static void test_write_protected_volume(void)
{
    status_$t status;
    dev_word[4] = 0x0800;
    DISK_VOL(1)->as_options = DISK_VOL_FLAG_WRITE_PROTECT;
    DISK_$WRITE_MULTI((int8_t)0xFF, blk(0), &status);
    ASSERT_EQ(status_$disk_write_protected, blk(0)->status);
    ASSERT_EQ(status_$disk_write_protected, blk(1)->status);
    ASSERT_EQ(1, n_map);                        /* volume 2 only */
}

static void test_map_failure_returns(void)
{
    status_$t status;
    dev_word[4] = 0x0800;
    map_status = 0x00080012;
    DISK_$WRITE_MULTI((int8_t)0xFF, blk(0), &status);
    ASSERT_EQ(0x00080012, status);
    ASSERT_EQ(1, n_map);
    ASSERT_EQ(0, n_do_io);
}

static void test_checksum_device_is_synchronous(void)
{
    status_$t status;
    dev_word[4] = 0x4800;                       /* checksum wins */
    DISK_$WRITE_MULTI((int8_t)0xFF, blk(0), &status);
    ASSERT_EQ(4, n_io);
    ASSERT_EQ(0, n_map);
}

int main(void)
{
    printf("DISK_$WRITE_MULTI tests:\n");
    RUN_TEST(set_chain_status);
    RUN_TEST(sync_runs_and_mount_failure);
    RUN_TEST(owner_mode_needs_state_2_and_owner);
    RUN_TEST(queued_and_postprocessed);
    RUN_TEST(write_protected_volume);
    RUN_TEST(map_failure_returns);
    RUN_TEST(checksum_device_is_synchronous);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
