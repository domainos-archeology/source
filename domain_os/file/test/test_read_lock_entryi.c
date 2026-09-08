/*
 * file/test/test_read_lock_entryi.c - FILE_$READ_LOCK_ENTRYI (0x00E6093C)
 *
 * The regression bead source-x07q is about: the routine builds its 34-byte
 * answer in a LOCAL at A6-0x28 and blits it into the caller's buffer at
 * 0x00E60BAE, on EVERY exit that reaches 0x00E60BA6 - including the
 * "no more entries" exit at 0x00E60ACE, which arrives with the local
 * untouched.  Only the two early returns at 0x00E60996 (a failed
 * DISK_$LVUID_TO_VOLX) and 0x00E609AC (the boot volume) skip the copy, and
 * those also leave the index alone.
 *
 * The real file/read_lock_entryi.c is #included at the bottom; everything it
 * calls is mocked here.
 */

#include <stdio.h>
#include <string.h>

#include "file/file_internal.h"
#include "disk/disk.h"
#include "cal/cal.h"

/* ==========================================================================
 * Test framework
 * ========================================================================== */

static int tests_passed = 0;
static int tests_failed = 0;
static int current_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do {                     \
    printf("  Running %-50s ", #name);          \
    current_failed = 0;                         \
    test_##name();                              \
    if (current_failed) { tests_failed++; }     \
    else { tests_passed++; printf("PASSED\n"); }\
} while (0)

#define ASSERT_EQ(expected, actual) do {                                 \
    unsigned long long _e = (unsigned long long)(expected);              \
    unsigned long long _a = (unsigned long long)(actual);                \
    if (_e != _a) {                                                      \
        printf("FAILED\n    Expected 0x%llx, got 0x%llx at line %d\n",   \
               _e, _a, __LINE__);                                        \
        current_failed = 1;                                              \
        return;                                                          \
    }                                                                    \
} while (0)

/* ==========================================================================
 * Globals the code under test reaches
 * ========================================================================== */

uint32_t NODE_$ME = 0x000ABCDE;
uint32_t ROUTE_$PORT = 0x11223344;
int16_t CAL_$BOOT_VOLX = 9;

file_lock_control_t FILE_$LOCK_CONTROL;
file_lock_entry_detail_t FILE_$LOCK_ENTRIES[FILE_LOCK_ENTRY_COUNT + 1];
file_lock_table_entry_t FILE_$LOCK_TABLE[FILE_LOCK_TABLE_ENTRIES];
uint16_t FILE_$LOCK_TABLE2[FILE_LOCK_TABLE_ENTRIES];

/* ==========================================================================
 * Mocks
 * ========================================================================== */

static int      ml_lock_calls;
static int      ml_unlock_calls;

void ML_$LOCK(int16_t id)   { (void)id; ml_lock_calls++; }
void ML_$UNLOCK(int16_t id) { (void)id; ml_unlock_calls++; }

static int       lvuid_calls;
static void     *lvuid_uid_seen;
static int16_t   lvuid_volx;
static status_$t lvuid_status;

void DISK_$LVUID_TO_VOLX(void *uid_ptr, int16_t *vol_idx, status_$t *status)
{
    lvuid_calls++;
    lvuid_uid_seen = uid_ptr;
    *vol_idx = lvuid_volx;
    *status = lvuid_status;
}

static int       verify_calls;
static void     *verify_info_seen;
static status_$t verify_status[8];
static int       verify_rewrites;      /* if set, the mock scribbles the record */

void FILE_$VERIFY_LOCK_HOLDER(file_lock_info_internal_t *lock_info,
                              status_$t *status_ret)
{
    verify_info_seen = lock_info;
    if (verify_rewrites) {
        lock_info->context = 0xC0DEC0DE;
    }
    *status_ret = verify_calls < 8 ? verify_status[verify_calls]
                                   : status_$ok;
    verify_calls++;
}

/* ==========================================================================
 * Code under test
 * ========================================================================== */

#include "../read_lock_entryi.c"

/* ==========================================================================
 * Helpers
 * ========================================================================== */

static uid_t     query_uid;
static uint16_t  index_io;
static file_lock_info_internal_t out;
static status_$t st;

static void reset(void)
{
    memset(&FILE_$LOCK_CONTROL, 0, sizeof(FILE_$LOCK_CONTROL));
    memset(FILE_$LOCK_ENTRIES, 0, sizeof(FILE_$LOCK_ENTRIES));
    memset(FILE_$LOCK_TABLE, 0, sizeof(FILE_$LOCK_TABLE));
    memset(FILE_$LOCK_TABLE2, 0, sizeof(FILE_$LOCK_TABLE2));

    ml_lock_calls = 0;
    ml_unlock_calls = 0;
    lvuid_calls = 0;
    lvuid_uid_seen = NULL;
    lvuid_volx = 3;
    lvuid_status = status_$ok;
    verify_calls = 0;
    verify_info_seen = NULL;
    verify_rewrites = 0;
    memset(verify_status, 0, sizeof(verify_status));

    /* A volume query: a non-zero first UID byte takes the DISK path. */
    query_uid.high = 0x40000000;
    query_uid.low  = 0;
    index_io = 0;

    /* The caller's buffer starts full of a recognisable pattern. */
    memset(&out, 0x5A, sizeof(out));
    st = 0x7F7F7F7F;

    FILE_$LOT_HIGH = 4;
}

static void call(void)
{
    FILE_$READ_LOCK_ENTRYI(&query_uid, &index_io, &out, &st);
}

/* Fill lock entry `n` (1-based) with a local, matching entry. */
static void seed_entry(uint16_t n, uint8_t volume)
{
    file_lock_entry_detail_t *e = FILE_$LOT_ENTRY(n);

    e->context   = 0x0000AA00 + n;
    e->node_low  = 0x0BB00000 + n;
    e->node_high = 0x0CC00000 + n;
    e->uid_high  = 0x11110000 + n;
    e->uid_low   = 0x22220000 + n;
    e->sequence  = (uint16_t)(0x300 + n);
    e->refcount  = 1;
    e->flags1    = volume;              /* bits 0..5 are the volume */
    e->flags2    = 0x80 | (0x3 << 3);   /* side 1, mode 3, not remote */
}

/* ==========================================================================
 * Tests
 * ========================================================================== */

/*
 * A found entry fills the record and hands it back, and the whole record -
 * not just the fields the arm wrote - comes from the local.
 */
TEST(found_entry_is_copied_out)
{
    reset();
    seed_entry(2, 3);
    call();

    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(3, index_io);                     /* found 2, next is 3 */
    ASSERT_EQ(0x11110002, out.file_uid.high);
    ASSERT_EQ(0x22220002, out.file_uid.low);
    ASSERT_EQ(0x0000AA02, out.context);
    ASSERT_EQ(1, out.side);
    ASSERT_EQ(3, out.mode);
    ASSERT_EQ(0x302, out.sequence);
    /* not remote: this node is the holder, the entry names the owner */
    ASSERT_EQ(NODE_$ME, out.holder_node);
    ASSERT_EQ(ROUTE_$PORT, out.holder_port);
    ASSERT_EQ(0x0BB00002, out.owner_node);
    ASSERT_EQ(0x0CC00002, out.remote_info);
    ASSERT_EQ(1, ml_lock_calls);
    ASSERT_EQ(1, ml_unlock_calls);
    ASSERT_EQ(1, verify_calls);
    /* the verifier is handed the LOCAL, so its address is not the caller's */
    ASSERT_EQ(1, verify_info_seen != (void *)&out);
}

/*
 * 0x00E60ACE -> 0x00E60BA6: the "no more entries" exit STILL runs the copy at
 * 0x00E60BAE, overwriting the caller's buffer with the untouched local.  The
 * tree used to leave the buffer alone here.
 */
TEST(not_found_still_overwrites_the_caller_buffer)
{
    reset();
    /* no entries seeded, so nothing matches */
    call();

    ASSERT_EQ(0x000F000C, st);
    ASSERT_EQ(0xFFFF, index_io);
    ASSERT_EQ(1, ml_lock_calls);
    ASSERT_EQ(1, ml_unlock_calls);
    ASSERT_EQ(0, verify_calls);

    /*
     * The record was copied even though nothing was found: the 0x5A pattern
     * the caller left is gone.  Its contents are frame leftovers, so only the
     * fact that they changed can be asserted.
     */
    {
        file_lock_info_internal_t pristine;
        memset(&pristine, 0x5A, sizeof(pristine));
        ASSERT_EQ(1, memcmp(&out, &pristine, sizeof(out)) != 0);
    }
}

/*
 * 0x00E6098A-0x00E60996: a failed DISK_$LVUID_TO_VOLX returns before the copy
 * and before the index write, and the UID it is handed is the FRAME COPY.
 */
TEST(lvuid_failure_returns_without_touching_the_buffer)
{
    file_lock_info_internal_t pristine;

    reset();
    memset(&pristine, 0x5A, sizeof(pristine));
    lvuid_status = 0x00050001;
    index_io = 7;
    call();

    ASSERT_EQ(0x00050001, st);
    ASSERT_EQ(7, index_io);                     /* untouched */
    ASSERT_EQ(0, memcmp(&out, &pristine, sizeof(out)));
    ASSERT_EQ(1, lvuid_calls);
    ASSERT_EQ(1, lvuid_uid_seen != (void *)&query_uid);   /* the frame copy */
    ASSERT_EQ(0, ml_lock_calls);
}

/* 0x00E6099A-0x00E609AC: the boot volume is refused the same way. */
TEST(boot_volume_returns_without_touching_the_buffer)
{
    file_lock_info_internal_t pristine;

    reset();
    memset(&pristine, 0x5A, sizeof(pristine));
    lvuid_volx = (int16_t)CAL_$BOOT_VOLX;
    index_io = 7;
    call();

    ASSERT_EQ(0x140002, st);
    ASSERT_EQ(7, index_io);
    ASSERT_EQ(0, memcmp(&out, &pristine, sizeof(out)));
    ASSERT_EQ(0, ml_lock_calls);
}

/*
 * 0x00E60B94-0x00E60BA2: the verifier works on the LOCAL, so what it writes
 * there is what the caller gets; and a 0x000F0005 answer resumes the scan
 * from 0x00E609E4 with the advanced index.
 */
TEST(verifier_edits_the_local_and_can_restart_the_scan)
{
    reset();
    seed_entry(2, 3);
    verify_rewrites = 1;
    call();
    ASSERT_EQ(0xC0DEC0DE, out.context);

    reset();
    seed_entry(2, 3);
    seed_entry(3, 3);
    verify_status[0] = file_$object_not_locked_by_this_process;
    verify_status[1] = status_$ok;
    call();

    ASSERT_EQ(2, verify_calls);
    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(4, index_io);                     /* the second pass found 3 */
    ASSERT_EQ(0x11110003, out.file_uid.high);
    ASSERT_EQ(2, ml_lock_calls);
    ASSERT_EQ(2, ml_unlock_calls);
}

/*
 * 0x00E60B80-0x00E60B8A: a zero first UID byte with a non-zero low longword
 * skips the verifier - and still copies the record out.  Since only
 * FILE_$VERIFY_LOCK_HOLDER ever overwrites the status cell seeded with
 * 0x000F000C at 0x00E609E4, this path reports "no more lock entries" WITH a
 * filled record and an advanced index.  Reproduced as found.
 */
TEST(zero_byte0_with_uid_low_skips_the_verifier)
{
    reset();
    /* byte0 == 0 but not the per-ASID shape, so the global table is walked */
    query_uid.high = 0x00020000;
    query_uid.low  = 0x99999999;
    seed_entry(2, 3);
    call();

    ASSERT_EQ(0, verify_calls);
    ASSERT_EQ(0x000F000C, st);
    ASSERT_EQ(3, index_io);
    ASSERT_EQ(0x11110002, out.file_uid.high);
    ASSERT_EQ(0, lvuid_calls);
}

/*
 * 0x00E60956-0x00E609D0: the per-ASID shape is byte0 == 0, byte1 == 1 and a
 * non-negative word below 0x3A; the sequence then comes from the refcount
 * byte instead of the sequence word (0x00E60B1A).
 */
TEST(per_asid_query)
{
    reset();
    query_uid.high = 0x00010004;        /* byte0 0, byte1 1, asid 4 */
    query_uid.low  = 0;

    FILE_$LOCK_TABLE2[4] = 2;           /* two slots in row 4 */
    FILE_$PROC_LOT_SLOT(4, 1) = 0;
    FILE_$PROC_LOT_SLOT(4, 2) = 5;      /* slot 2 names lock entry 5 */
    seed_entry(5, 1);
    FILE_$LOT_ENTRY(5)->refcount = 0x2A;

    call();

    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(3, index_io);             /* slot 2 found, next is 3 */
    ASSERT_EQ(0x11110005, out.file_uid.high);
    ASSERT_EQ(0x2A, out.sequence);      /* the refcount byte, not sequence */
    ASSERT_EQ(0, lvuid_calls);
}

/* 0x00E60A8C-0x00E60AA0: a non-zero volume filters the global walk. */
TEST(volume_filter)
{
    reset();
    seed_entry(1, 1);                   /* volume 1, does not match volx 3 */
    seed_entry(3, 3);                   /* volume 3, matches */
    lvuid_volx = 3;
    call();

    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(4, index_io);
    ASSERT_EQ(0x11110003, out.file_uid.high);

    /* A remote entry (bit 2 of flags2) is skipped by the volume filter. */
    reset();
    seed_entry(3, 3);
    FILE_$LOT_ENTRY(3)->flags2 |= 0x04;
    lvuid_volx = 3;
    call();
    ASSERT_EQ(0x000F000C, st);
}

/* 0x00E60B30-0x00E60B6C: the remote arm swaps holder and owner. */
TEST(remote_entry_swaps_holder_and_owner)
{
    reset();
    seed_entry(2, 3);
    FILE_$LOT_ENTRY(2)->flags2 |= 0x04;
    lvuid_volx = 0;                     /* no volume filter */
    call();

    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(0x0BB00002, out.holder_node);
    ASSERT_EQ(0x0CC00002, out.holder_port);
    ASSERT_EQ(NODE_$ME, out.owner_node);
    ASSERT_EQ(ROUTE_$PORT, out.remote_info);
}

int main(void)
{
    printf("FILE_$READ_LOCK_ENTRYI tests\n");
    RUN_TEST(found_entry_is_copied_out);
    RUN_TEST(not_found_still_overwrites_the_caller_buffer);
    RUN_TEST(lvuid_failure_returns_without_touching_the_buffer);
    RUN_TEST(boot_volume_returns_without_touching_the_buffer);
    RUN_TEST(verifier_edits_the_local_and_can_restart_the_scan);
    RUN_TEST(zero_byte0_with_uid_low_skips_the_verifier);
    RUN_TEST(per_asid_query);
    RUN_TEST(volume_filter);
    RUN_TEST(remote_entry_swaps_holder_and_owner);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
