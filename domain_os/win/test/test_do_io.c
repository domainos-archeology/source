/*
 * win/test/test_do_io.c - WIN_$DO_IO (0x00E19776)
 *
 * SEEK, read_or_write_disk_record, win_$reinit_drive, WIN_$FORMAT_TRACK,
 * DISK_$SORT, EC_$WAIT, PARITY_$CHK_IO and the ML lock are mocked.  The
 * module block is WIN_$DATA; requests and the volume sit in a VA arena and
 * the DISK per-process slots are the host array disk.h declares.  The interrupt handler is simulated by the
 * EC_$WAIT mock, which posts a scripted status to the module's status word.
 */

#include <stdio.h>
#include <string.h>

#include "win/win_internal.h"
#include "parity/parity.h"

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

uint8_t WIN_$DATA[WIN_DATA_SIZE];
uint32_t TIME_$CLOCKH;
uint32_t win_$host_clockh(void) { return TIME_$CLOCKH; }

#define ARENA_SIZE      0x1000
#define ARENA_VA_BASE   0x00E7A000u
static uint8_t va_arena[ARENA_SIZE];
#define REQ_A_OFF   0x100
#define REQ_B_OFF   0x180
#define VOL_OFF     0x200
#define REQ_A ((win_$request_t *)(va_arena + REQ_A_OFF))
#define REQ_B ((win_$request_t *)(va_arena + REQ_B_OFF))
#define VOL   ((disk_$volume_t *)(va_arena + VOL_OFF))
#define VA(off) (ARENA_VA_BASE + (off))
disk_$per_proc_t DISK_$PER_PROC[DISK_PER_PROC_ENTRIES];
#define PER_PROC(pid) (DISK_$PER_PROC[pid])
#define WIN_STATUS (*(status_$t *)(WIN_$DATA + WIN_STATUS_OFFSET))

static int lock_calls, unlock_calls;
static int16_t lock_id;
void ML_$LOCK(int16_t id)   { lock_calls++; lock_id = id; }
void ML_$UNLOCK(int16_t id) { unlock_calls++; (void)id; }

static int format_calls;
void WIN_$FORMAT_TRACK(void *dev_entry, win_$request_t *req) { (void)dev_entry; (void)req; format_calls++; }

static int sort_calls;
static void *sort_new_head;
void DISK_$SORT(void *dev_entry, void **queue_ptr) { (void)dev_entry; sort_calls++; *queue_ptr = sort_new_head; }

/* SEEK: scripted per call */
static int seek_calls;
static status_$t seek_status[40];
static uint16_t seek_dev_unit;
static void *seek_req[40];
status_$t SEEK(uint16_t unit, uint16_t dev_unit, void *req, uint8_t flags)
{
    int n = seek_calls++;
    (void)unit; (void)flags;
    seek_dev_unit = dev_unit;
    if (n < 40) { seek_req[n] = req; return seek_status[n]; }
    return status_$ok;
}

static int rw_calls;
static status_$t rw_status;
status_$t read_or_write_disk_record(uint16_t unit) { (void)unit; rw_calls++; return rw_status; }

static int reinit_calls;
static status_$t reinit_status;
status_$t win_$reinit_drive(uint16_t unit, uint16_t dev_unit) { (void)unit; (void)dev_unit; reinit_calls++; return reinit_status; }

/* EC_$WAIT: posts the scripted completion status, returns the index */
static int wait_calls;
static status_$t wait_posts[40];
static int16_t wait_result;
static ec_$wait_ecs_t wait_ecs;
static ec_$wait_vals_t wait_vals;
int16_t EC_$WAIT(ec_$wait_ecs_t ecs, ec_$wait_vals_t vals)
{
    int n = wait_calls++;
    wait_ecs = ecs; wait_vals = vals;
    if (n < 40) { WIN_STATUS = wait_posts[n]; }
    return wait_result;
}

static int parity_calls;
static uint32_t parity_a, parity_b, parity_result;
uint32_t PARITY_$CHK_IO(uint32_t a, uint32_t b) { parity_calls++; parity_a = a; parity_b = b; return parity_result; }

#include "../do_io.c"

static int8_t result;

static void reset(void)
{
    memset(WIN_$DATA, 0, sizeof(WIN_$DATA));
    memset(va_arena, 0, sizeof(va_arena));
    *(int16_t *)(WIN_$DATA + WIN_DEV_TYPE_OFFSET) = 0x31;
    WIN_UNIT_EC(0)->value = 100;
    TIME_$CLOCKH = 5000;
    VOL->dev_unit = 3;
    REQ_A->next = 0;
    REQ_A->pa = 0xABC00;
    REQ_A->length = 0x400;
    REQ_A->proc_id = 9;
    REQ_A->flags = 0x02;
    REQ_A->status = 0x11111111;
    REQ_B->next = 0;
    REQ_B->status = 0x22222222;
    PER_PROC(9).io_pending = -1;
    result = 0x5A;
    lock_calls = unlock_calls = 0;
    format_calls = 0; sort_calls = 0; sort_new_head = REQ_B;
    seek_calls = 0; memset(seek_status, 0, sizeof(seek_status));
    rw_calls = 0; rw_status = status_$ok;
    reinit_calls = 0; reinit_status = status_$ok;
    wait_calls = 0; memset(wait_posts, 0, sizeof(wait_posts)); wait_result = 0;
    parity_calls = 0; parity_result = 0;
}

/* 0x00E19798-0x00E197BE: op 3 -> lock, format, unlock; nothing else. */
TEST(format_request)
{
    reset();
    REQ_A->flags = 0x03;
    WIN_$DO_IO(VOL, REQ_A, NULL, &result);
    ASSERT_EQ(0, result);
    ASSERT_EQ(1, format_calls);
    ASSERT_EQ(1, lock_calls);
    ASSERT_EQ(0x31, lock_id);
    ASSERT_EQ(1, unlock_calls);
    ASSERT_EQ(0, seek_calls);
    ASSERT_EQ(0, WIN_CUR_REQ_VA);
}

/* The plain success path: seek(0, dev_unit, A, 0), transfer, wait on unit
 * 0's eventcount + 1 with an 8-tick clock timeout, status 0 -> done. */
TEST(single_request_success)
{
    reset();
    WIN_$DO_IO(VOL, REQ_A, NULL, &result);
    ASSERT_EQ(VA(REQ_A_OFF), WIN_CUR_REQ_VA);
    ASSERT_EQ(VA(VOL_OFF), WIN_DEV_INFO_VA);
    ASSERT_EQ(1, seek_calls);
    ASSERT_EQ(3, seek_dev_unit);
    ASSERT_EQ((long long)(intptr_t)REQ_A, (long long)(intptr_t)seek_req[0]);
    ASSERT_EQ(1, rw_calls);
    ASSERT_EQ(1, wait_calls);
    ASSERT_EQ((long long)(intptr_t)WIN_UNIT_EC(0), (long long)(intptr_t)wait_ecs.ec[0]);
    ASSERT_EQ((long long)(intptr_t)&TIME_$CLOCKH, (long long)(intptr_t)wait_ecs.ec[1]);
    ASSERT_EQ(0, (long long)(intptr_t)wait_ecs.ec[2]);
    ASSERT_EQ(101, wait_vals.val[0]);
    ASSERT_EQ(5008, wait_vals.val[1]);
    ASSERT_EQ(0, wait_vals.val[2]);
    ASSERT_EQ(0x11111111, REQ_A->status);
    ASSERT_EQ(-1, PER_PROC(9).io_pending);
    ASSERT_EQ(1, unlock_calls);
}

/* 0x00E197C2-0x00E197E0: a write chain of >1 is sorted; the new head is
 * what gets driven. */
TEST(write_chain_is_sorted)
{
    reset();
    REQ_A->next = VA(REQ_B_OFF);
    WIN_$DO_IO(VOL, REQ_A, NULL, &result);
    ASSERT_EQ(1, sort_calls);
    ASSERT_EQ((long long)(intptr_t)REQ_B, (long long)(intptr_t)seek_req[0]);

    reset();
    REQ_A->next = VA(REQ_B_OFF);
    REQ_A->flags = 0x01;
    WIN_$DO_IO(VOL, REQ_A, NULL, &result);
    ASSERT_EQ(0, sort_calls);
}

/* A timeout wake-up: the flag is cleared and the timeout status posted,
 * which then goes through the retry path (24 times) and fails. */
TEST(timeout_retries_then_fails)
{
    reset();
    wait_result = 1;
    WIN_$DO_IO(VOL, REQ_A, NULL, &result);
    ASSERT_EQ(24, seek_calls);                  /* 1 + 23 dbf retries */
    ASSERT_EQ(status_$disk_controller_timeout, REQ_A->status);
    ASSERT_EQ(0, PER_PROC(9).io_pending);
    ASSERT_EQ(0, WIN_CUR_REQ_VA);
    ASSERT_EQ(0, WIN_$DATA[WIN_FLAG_OFFSET]);
}

/* DMA overrun uses its own 500-count budget and does not consume the dbf
 * budget. */
TEST(dma_overrun_budget)
{
    int i;
    reset();
    for (i = 0; i < 40; i++) wait_posts[i] = status_$DMA_overrun;
    /* after 3 overruns, success */
    wait_posts[3] = status_$ok;
    WIN_$DO_IO(VOL, REQ_A, NULL, &result);
    ASSERT_EQ(4, seek_calls);
    ASSERT_EQ(0x11111111, REQ_A->status);
}

/* Parity error: fatal iff the checker's low word is non-zero. */
TEST(parity_error)
{
    reset();
    wait_posts[0] = status_$memory_parity_error_during_disk_write;
    parity_result = 0x00010000;                 /* high word only */
    wait_posts[1] = status_$ok;
    WIN_$DO_IO(VOL, REQ_A, NULL, &result);
    ASSERT_EQ(1, parity_calls);
    ASSERT_EQ(0xABC00 >> 10, parity_a);
    ASSERT_EQ(0x400, parity_b);
    ASSERT_EQ(2, seek_calls);
    ASSERT_EQ(0x11111111, REQ_A->status);

    reset();
    wait_posts[0] = status_$memory_parity_error_during_disk_write;
    parity_result = 1;
    WIN_$DO_IO(VOL, REQ_A, NULL, &result);
    ASSERT_EQ(1, seek_calls);
    ASSERT_EQ(status_$memory_parity_error_during_disk_write, REQ_A->status);
}

/* Data check: fatal on a checksummed request, retried otherwise. */
TEST(data_check)
{
    reset();
    wait_posts[0] = status_$disk_data_check;
    REQ_A->flags = (int8_t)0x82;
    WIN_$DO_IO(VOL, REQ_A, NULL, &result);
    ASSERT_EQ(1, seek_calls);
    ASSERT_EQ(status_$disk_data_check, REQ_A->status);

    reset();
    wait_posts[0] = status_$disk_data_check;
    WIN_$DO_IO(VOL, REQ_A, NULL, &result);
    ASSERT_EQ(2, seek_calls);
    ASSERT_EQ(0, reinit_calls);
    ASSERT_EQ(0x11111111, REQ_A->status);
}

/* Not ready / unknown / equipment check re-initialise the drive first. */
TEST(reinit_statuses)
{
    reset();
    wait_posts[0] = status_$disk_not_ready;
    wait_posts[1] = status_$unknown_error_status_from_drive;
    wait_posts[2] = status_$disk_equipment_check;
    WIN_$DO_IO(VOL, REQ_A, NULL, &result);
    ASSERT_EQ(3, reinit_calls);
    ASSERT_EQ(4, seek_calls);
    ASSERT_EQ(0x11111111, REQ_A->status);
}

/* A seek error re-initialises; a failing re-init replaces the status. */
TEST(seek_error)
{
    reset();
    seek_status[0] = status_$disk_seek_error;
    WIN_$DO_IO(VOL, REQ_A, NULL, &result);
    ASSERT_EQ(1, reinit_calls);
    ASSERT_EQ(2, seek_calls);
    ASSERT_EQ(0, wait_calls == 0);              /* the retry waited */
    ASSERT_EQ(0x11111111, REQ_A->status);

    reset();
    memset(seek_status, 0, sizeof(seek_status));
    for (int i = 0; i < 40; i++) seek_status[i] = status_$disk_seek_error;
    reinit_status = status_$disk_equipment_check;
    WIN_$DO_IO(VOL, REQ_A, NULL, &result);
    ASSERT_EQ(24, seek_calls);
    ASSERT_EQ(status_$disk_equipment_check, REQ_A->status);
}

/* A positive transfer status (no interrupt expected) is dispatched without
 * waiting; a negative one waits. */
TEST(transfer_status_polarity)
{
    reset();
    rw_status = status_$disk_not_ready;
    WIN_$DO_IO(VOL, REQ_A, NULL, &result);
    ASSERT_EQ(24, reinit_calls);                /* every attempt re-inits */
    ASSERT_EQ(0, wait_calls);                   /* and none of them waits */
    ASSERT_EQ(status_$disk_not_ready, REQ_A->status);

    reset();
    rw_status = (status_$t)-1;
    WIN_$DO_IO(VOL, REQ_A, NULL, &result);
    ASSERT_EQ(1, wait_calls);
    ASSERT_EQ(0, reinit_calls);
}

/* Failure with a chain: the current request gets the status, those behind
 * it -1, and the cell walks to NULL. */
TEST(failure_fails_the_rest_of_the_chain)
{
    reset();
    REQ_A->next = VA(REQ_B_OFF);
    REQ_A->flags = 0x01;
    wait_posts[0] = status_$memory_parity_error_during_disk_write;
    parity_result = 1;
    WIN_$DO_IO(VOL, REQ_A, NULL, &result);
    ASSERT_EQ(status_$memory_parity_error_during_disk_write, REQ_A->status);
    ASSERT_EQ((status_$t)-1, REQ_B->status);
    ASSERT_EQ(0, WIN_CUR_REQ_VA);
    ASSERT_EQ(0, PER_PROC(9).io_pending);
}

int main(void)
{
    ARCH_HOST_VA_BASE = (uintptr_t)va_arena - ARENA_VA_BASE;
    printf("WIN_$DO_IO tests\n");
    RUN_TEST(format_request);
    RUN_TEST(single_request_success);
    RUN_TEST(write_chain_is_sorted);
    RUN_TEST(timeout_retries_then_fails);
    RUN_TEST(dma_overrun_budget);
    RUN_TEST(parity_error);
    RUN_TEST(data_check);
    RUN_TEST(reinit_statuses);
    RUN_TEST(seek_error);
    RUN_TEST(transfer_status_polarity);
    RUN_TEST(failure_fails_the_rest_of_the_chain);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
