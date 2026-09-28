/*
 * vtoc/test/test_get_uid.c - unit tests for VTOC_$GET_UID (0x00E391F2)
 *
 * Drives the real routine through mocked DBUF_$GET_BLOCK / DBUF_$SET_BUFF
 * over a small in-memory "disk" of 1K blocks.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;

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

#include "vtoc/vtoc_internal.h"

vtoc_$data_t vtoc_$data;
uid_t VTOC_$UID = { 0x00000202u, 0 };
uid_t VTOC_BKT_$UID = { 0x00000203u, 0 };

#define NBLOCKS 8
static uint8_t disk[NBLOCKS][0x400];

static int       mock_lock_depth;
static int       mock_get_calls;
static uint32_t  mock_get_block[8];
static uid_t     mock_get_uid[8];
static status_$t mock_get_status;
static int       mock_set_calls;
static void     *mock_set_buf[8];
static uint16_t  mock_set_op[8];
static status_$t *mock_set_status_p[8];

void ML_$LOCK(int16_t id)   { (void)id; mock_lock_depth++; }
void ML_$UNLOCK(int16_t id) { (void)id; mock_lock_depth--; }

void *DBUF_$GET_BLOCK(uint16_t vol_idx, int32_t block, uid_t *uid,
                      uint32_t hint, uint16_t type, uint16_t flags, status_$t *status)
{
    (void)vol_idx; (void)hint; (void)type; (void)flags;
    mock_get_block[mock_get_calls] = (uint32_t)block;
    mock_get_uid[mock_get_calls] = *uid;
    mock_get_calls++;
    *status = mock_get_status;
    if (mock_get_status != status_$ok || block < 1 || block >= NBLOCKS) {
        return NULL;
    }
    return disk[block];
}

void DBUF_$SET_BUFF(void *buf, uint16_t op, status_$t *status)
{
    mock_set_buf[mock_set_calls] = buf;
    mock_set_op[mock_set_calls] = op;
    mock_set_status_p[mock_set_calls] = status;
    mock_set_calls++;
    *status = status_$ok;
}

#include "vtoc/get_uid.c"

#define VOL 3

static void reset_state(void)
{
    vtoc_$vol_t *vol;

    memset(&vtoc_$data, 0, sizeof(vtoc_$data));
    memset(disk, 0, sizeof(disk));
    mock_lock_depth = 0;
    mock_get_calls = 0;
    mock_get_status = status_$ok;
    mock_set_calls = 0;
    memset(mock_set_buf, 0, sizeof(mock_set_buf));

    vtoc_$data.mounted[VOL] = (int8_t)0xFF;
    vol = VTOC_VOL(VOL);
    vol->parts[0].count = 2;  vol->parts[0].base = 1;   /* blocks 1,2 */
    vol->parts[1].count = 2;  vol->parts[1].base = 3;   /* blocks 3,4 */
    vol->current_vtoce = 0;
}

static vtoce_$old_disk_t *old_entry(int block, int k)
{
    return &((vtoc_$old_block_t *)disk[block])->entries[k];
}

/* old format: partition 1 offset 1 = block 4, entry 2 in use -> its UID,
 * status ok, buffer released into the throw-away cell */
static void test_old_format_direct_hit(void)
{
    int16_t vol = VOL; uint16_t vidx = 3; uint16_t eidx = 2;
    uid_t out = { 0, 0 }; status_$t status = 0x77;

    old_entry(4, 2)->hdr.status = VTOCE_STATUS_IN_USE;
    old_entry(4, 2)->hdr.uid.high = 0xABCD0001u;
    old_entry(4, 2)->hdr.uid.low = 0x00000002u;

    VTOC_$GET_UID(&vol, &vidx, &eidx, &out, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(0xABCD0001u, out.high);
    ASSERT_EQ(0x00000002u, out.low);
    ASSERT_EQ(1, mock_get_calls);
    ASSERT_EQ(4, mock_get_block[0]);
    ASSERT_EQ(VTOC_$UID.high, mock_get_uid[0].high);
    ASSERT_EQ(1, mock_set_calls);
    ASSERT_EQ((unsigned long)disk[4], (unsigned long)mock_set_buf[0]);
    ASSERT_EQ(BAT_BUF_CLEAN, mock_set_op[0]);
    ASSERT_EQ(0, mock_lock_depth);
}

/* entry index past the five entries follows the chain word, subtracting 5 */
static void test_old_format_follows_chain(void)
{
    int16_t vol = VOL; uint16_t vidx = 0; uint16_t eidx = 6;
    uid_t out = { 0, 0 }; status_$t status = 0x77;

    ((vtoc_$old_block_t *)disk[1])->next_block = 5;
    old_entry(5, 1)->hdr.status = VTOCE_STATUS_IN_USE;
    old_entry(5, 1)->hdr.uid.high = 0x55u;

    VTOC_$GET_UID(&vol, &vidx, &eidx, &out, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(0x55u, out.high);
    ASSERT_EQ(2, mock_get_calls);
    ASSERT_EQ(1, mock_get_block[0]);
    ASSERT_EQ(5, mock_get_block[1]);
    ASSERT_EQ(2, mock_set_calls);
    ASSERT_EQ((unsigned long)disk[1], (unsigned long)mock_set_buf[0]);
    /* the in-loop release reports into the status cell, not the throw-away
     * one (0x00E392C4 pea (-0x14,A6)) */
    ASSERT_EQ((unsigned long)mock_set_status_p[0] != (unsigned long)mock_set_status_p[1], 1);
}

/* a free entry, or the volume's current VTOCE, is "no UID" (0x20004); the
 * UID is still copied out */
static void test_old_format_no_uid_cases(void)
{
    int16_t vol = VOL; uint16_t vidx = 0; uint16_t eidx = 0;
    uid_t out = { 0, 0 }; status_$t status = 0x77;

    old_entry(1, 0)->hdr.uid.high = 0x99u;
    VTOC_$GET_UID(&vol, &vidx, &eidx, &out, &status);
    ASSERT_EQ(status_$no_UID, status);
    ASSERT_EQ(0x99u, out.high);

    reset_state();
    old_entry(1, 0)->hdr.status = VTOCE_STATUS_IN_USE;
    old_entry(1, 0)->hdr.uid.high = 0x99u;
    VTOC_VOL(VOL)->current_vtoce = (1u << 4) | 0;   /* block 1, entry 0 */
    VTOC_$GET_UID(&vol, &vidx, &eidx, &out, &status);
    ASSERT_EQ(status_$no_UID, status);

    reset_state();
    old_entry(1, 0)->hdr.status = VTOCE_STATUS_IN_USE;
    VTOC_VOL(VOL)->current_vtoce = (1u << 4) | 1;   /* same block, other entry */
    VTOC_$GET_UID(&vol, &vidx, &eidx, &out, &status);
    ASSERT_EQ(status_$ok, status);
}

/* chain exhausted: 0x20005 */
static void test_old_format_not_found(void)
{
    int16_t vol = VOL; uint16_t vidx = 0; uint16_t eidx = 5;
    uid_t out = { 0, 0 }; status_$t status = 0x77;

    VTOC_$GET_UID(&vol, &vidx, &eidx, &out, &status);

    ASSERT_EQ(status_$VTOC_not_found, status);
    ASSERT_EQ(1, mock_get_calls);
    ASSERT_EQ(VTOC_$UID.high, out.high);          /* local_uid never replaced */
}

/* vtoc_idx beyond every partition: block 0 -> not found before any I/O */
static void test_beyond_partitions(void)
{
    int16_t vol = VOL; uint16_t vidx = 4; uint16_t eidx = 0;
    uid_t out = { 0, 0 }; status_$t status = 0x77;

    VTOC_$GET_UID(&vol, &vidx, &eidx, &out, &status);

    ASSERT_EQ(status_$VTOC_not_found, status);
    ASSERT_EQ(0, mock_get_calls);
    ASSERT_EQ(0, mock_set_calls);
}

/* new format: vtoc_idx low bits pick the bucket, entries counted through
 * next_bkt_idx / next_bucket; block_info != current_vtoce is success */
static void test_new_format_bucket_chain(void)
{
    int16_t vol = VOL; uint16_t vidx = (1 << 2) | 2; uint16_t eidx = 21;
    uid_t out = { 0, 0 }; status_$t status = 0x77;
    vtoc_$bucket_entry_t *b2 = &((vtoc_$bkt_block_t *)disk[2])->buckets[2];
    vtoc_$bucket_entry_t *b6 = &((vtoc_$bkt_block_t *)disk[6])->buckets[3];

    vtoc_$data.format[VOL] = (int8_t)0xFF;
    b2->next_bucket = 6;
    b2->next_bkt_idx = 3;
    b6->slots[1].uid.high = 0x7777u;
    b6->slots[1].block_info = 0x1230;

    VTOC_$GET_UID(&vol, &vidx, &eidx, &out, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(0x7777u, out.high);
    ASSERT_EQ(2, mock_get_calls);
    ASSERT_EQ(2, mock_get_block[0]);              /* part 0 base 1 + idx 1 */
    ASSERT_EQ(VTOC_BKT_$UID.high, mock_get_uid[0].high);
    ASSERT_EQ(6, mock_get_block[1]);
}

/* new format: an empty slot or the current VTOCE is "no UID" */
static void test_new_format_no_uid(void)
{
    int16_t vol = VOL; uint16_t vidx = 0; uint16_t eidx = 0;
    uid_t out = { 0, 0 }; status_$t status = 0x77;
    vtoc_$bucket_entry_t *b = &((vtoc_$bkt_block_t *)disk[1])->buckets[0];

    vtoc_$data.format[VOL] = (int8_t)0xFF;
    VTOC_$GET_UID(&vol, &vidx, &eidx, &out, &status);
    ASSERT_EQ(status_$no_UID, status);

    reset_state();
    vtoc_$data.format[VOL] = (int8_t)0xFF;
    b->slots[0].block_info = 0x4440;
    VTOC_VOL(VOL)->current_vtoce = 0x4440;
    VTOC_$GET_UID(&vol, &vidx, &eidx, &out, &status);
    ASSERT_EQ(status_$no_UID, status);
}

/* not mounted: 0x20001, and the original's quirk - A2 still holds the
 * caller's entry_idx pointer, which is released as if it were a buffer */
static void test_not_mounted_quirk(void)
{
    int16_t vol = VOL; uint16_t vidx = 0; uint16_t eidx = 0;
    uid_t out = { 0, 0 }; status_$t status = 0x77;

    vtoc_$data.mounted[VOL] = 0;
    VTOC_$GET_UID(&vol, &vidx, &eidx, &out, &status);

    ASSERT_EQ(status_$VTOC_not_mounted, status);
    ASSERT_EQ(0, mock_get_calls);
    ASSERT_EQ(1, mock_set_calls);
    ASSERT_EQ((unsigned long)&eidx, (unsigned long)mock_set_buf[0]);
    ASSERT_EQ(0, mock_lock_depth);
}

/* a DBUF failure is passed through; no release of the NULL buffer */
static void test_get_block_failure(void)
{
    int16_t vol = VOL; uint16_t vidx = 0; uint16_t eidx = 0;
    uid_t out = { 0, 0 }; status_$t status = 0x77;

    mock_get_status = 0x00080001;
    VTOC_$GET_UID(&vol, &vidx, &eidx, &out, &status);

    ASSERT_EQ(0x00080001, status);
    ASSERT_EQ(0, mock_set_calls);
}

int main(void)
{
    printf("VTOC_$GET_UID tests:\n");
    RUN_TEST(old_format_direct_hit);
    RUN_TEST(old_format_follows_chain);
    RUN_TEST(old_format_no_uid_cases);
    RUN_TEST(old_format_not_found);
    RUN_TEST(beyond_partitions);
    RUN_TEST(new_format_bucket_chain);
    RUN_TEST(new_format_no_uid);
    RUN_TEST(not_mounted_quirk);
    RUN_TEST(get_block_failure);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
