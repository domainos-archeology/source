/*
 * cal/test/test_read_timezone.c - Unit tests for CAL_$READ_TIMEZONE
 * (0x00E3E520)
 *
 * The real cal/read_timezone.c and cal/cal_data.c are #included; the PROC1
 * lock, DBUF and NETWORK dependencies are mocked and record their calls.
 * The label buffer is a host bat_$label_t whose timezone fields are written
 * through the same cal_$label_tz_t view the code reads them with.
 */

#include "cal_test.h"

#include "dbuf/dbuf.h"
#include "proc1/proc1.h"
#include "uid/uid.h"
#include "network/network.h"

/* ==========================================================================
 * Globals the code under test links against
 * ========================================================================== */

int8_t NETWORK_$DISKLESS;
uid_t LV_LABEL_$UID = { 0x11111111, 0x22222222 };

/* ==========================================================================
 * Mocked callees
 * ========================================================================== */

static int set_lock_calls, clr_lock_calls;
static uint16_t last_lock_id;
static int lock_held;

void PROC1_$SET_LOCK(uint16_t lock_id)
{
    set_lock_calls++;
    last_lock_id = lock_id;
    lock_held++;
}

void PROC1_$CLR_LOCK(uint16_t lock_id)
{
    clr_lock_calls++;
    last_lock_id = lock_id;
    lock_held--;
}

static bat_$label_t label;
static int get_block_calls;
static uint16_t gb_vol_idx;
static int32_t gb_block;
static uid_t *gb_uid;
static uint32_t gb_hint;
static uint16_t gb_type, gb_flags;
static status_$t gb_status_value;
static int lock_held_at_get;

void *DBUF_$GET_BLOCK(uint16_t vol_idx, int32_t block, uid_t *uid,
                      uint32_t block_hint, uint16_t block_type,
                      uint16_t flags, status_$t *status)
{
    get_block_calls++;
    gb_vol_idx = vol_idx;
    gb_block = block;
    gb_uid = uid;
    gb_hint = block_hint;
    gb_type = block_type;
    gb_flags = flags;
    lock_held_at_get = lock_held;
    *status = gb_status_value;
    return &label;
}

static int set_buff_calls;
static void *sb_buffer;
static uint16_t sb_flags;
static int lock_held_at_set;

void DBUF_$SET_BUFF(void *buffer, uint16_t flags, status_$t *status)
{
    set_buff_calls++;
    sb_buffer = buffer;
    sb_flags = flags;
    lock_held_at_set = lock_held;
    *status = 0x00080001;   /* must not leak into the caller's status */
}

/* ==========================================================================
 * Code under test
 * ========================================================================== */

#include "../cal_data.c"
#include "../read_timezone.c"

static void reset(void)
{
    memset(&label, 0, sizeof(label));
    memset(&CAL_$TIMEZONE, 0, sizeof(CAL_$TIMEZONE));
    CAL_$LAST_VALID_TIME = 0;
    CAL_$BOOT_VOLX = 3;
    NETWORK_$DISKLESS = 0;
    set_lock_calls = clr_lock_calls = 0;
    lock_held = 0;
    get_block_calls = set_buff_calls = 0;
    gb_status_value = status_$ok;
}

/* ==========================================================================
 * Tests
 * ========================================================================== */

TEST(reads_label_fields_under_lock)
{
    cal_$timezone_rec_t out;
    status_$t status = 0x5555;
    cal_$label_tz_t *ltz = CAL_$LABEL_TZ(&label);

    reset();
    CAL_$TIMEZONE.drift.high = 0xD1;
    CAL_$TIMEZONE.drift.low = 0xD2;
    ltz->utc_delta = -300;
    ltz->tz_name[0] = 'E'; ltz->tz_name[1] = 'S';
    ltz->tz_name[2] = 'T'; ltz->tz_name[3] = ' ';
    ltz->last_valid_time = 0x12345678;

    CAL_$READ_TIMEZONE(&out, &status);

    ASSERT_EQ(status, status_$ok);
    ASSERT_EQ(set_lock_calls, 1);
    ASSERT_EQ(clr_lock_calls, 1);
    ASSERT_EQ(last_lock_id, 0xE);
    ASSERT_EQ(lock_held, 0);

    ASSERT_EQ(get_block_calls, 1);
    ASSERT_EQ(gb_vol_idx, 3);
    ASSERT_EQ(gb_block, 0);
    ASSERT_TRUE(gb_uid == &LV_LABEL_$UID);
    ASSERT_EQ(gb_hint, 0);
    ASSERT_EQ(gb_type, 0);
    ASSERT_EQ(gb_flags, 0);
    ASSERT_EQ(lock_held_at_get, 1);

    ASSERT_EQ(set_buff_calls, 1);
    ASSERT_TRUE(sb_buffer == &label);
    ASSERT_EQ(sb_flags, DBUF_FLAG_RELEASE);
    ASSERT_EQ(lock_held_at_set, 1);

    /* globals refreshed from the label; drift untouched */
    ASSERT_EQ((uint16_t)CAL_$TIMEZONE.utc_delta, (uint16_t)-300);
    ASSERT_EQ(CAL_$TIMEZONE.tz_name[0], 'E');
    ASSERT_EQ(CAL_$TIMEZONE.tz_name[3], ' ');
    ASSERT_EQ(CAL_$TIMEZONE.drift.high, 0xD1);
    ASSERT_EQ(CAL_$LAST_VALID_TIME, 0x12345678);

    /* and copied out */
    ASSERT_EQ((uint16_t)out.utc_delta, (uint16_t)-300);
    ASSERT_EQ(out.tz_name[2], 'T');
    ASSERT_EQ(out.drift.high, 0xD1);
    ASSERT_EQ(out.drift.low, 0xD2);
}

TEST(get_block_failure_returns_status_without_copy)
{
    cal_$timezone_rec_t out;
    status_$t status = 0;

    reset();
    memset(&out, 0xAA, sizeof(out));
    CAL_$TIMEZONE.utc_delta = 60;
    gb_status_value = 0x00080005;

    CAL_$READ_TIMEZONE(&out, &status);

    ASSERT_EQ(status, 0x00080005);
    ASSERT_EQ(set_lock_calls, 1);
    ASSERT_EQ(clr_lock_calls, 1);
    ASSERT_EQ(set_buff_calls, 0);
    ASSERT_EQ((uint8_t)out.tz_name[0], 0xAA);    /* not copied */
    ASSERT_EQ(CAL_$TIMEZONE.utc_delta, 60);      /* not refreshed */
}

TEST(diskless_copies_memory_record_only)
{
    cal_$timezone_rec_t out;
    status_$t status = 0x5555;

    reset();
    NETWORK_$DISKLESS = (int8_t)0xFF;
    CAL_$TIMEZONE.utc_delta = 540;
    CAL_$TIMEZONE.tz_name[0] = 'J';
    CAL_$LAST_VALID_TIME = 7;

    CAL_$READ_TIMEZONE(&out, &status);

    ASSERT_EQ(status, status_$ok);
    ASSERT_EQ(set_lock_calls, 0);
    ASSERT_EQ(get_block_calls, 0);
    ASSERT_EQ(out.utc_delta, 540);
    ASSERT_EQ(out.tz_name[0], 'J');
    ASSERT_EQ(CAL_$LAST_VALID_TIME, 7);
}

int main(void)
{
    printf("test_read_timezone:\n");

    RUN_TEST(reads_label_fields_under_lock);
    RUN_TEST(get_block_failure_returns_status_without_copy);
    RUN_TEST(diskless_copies_memory_record_only);

    printf("\n=== Results: %d passed, %d failed ===\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
