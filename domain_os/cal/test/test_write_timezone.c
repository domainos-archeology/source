/*
 * cal/test/test_write_timezone.c - Unit tests for CAL_$WRITE_TIMEZONE
 * (0x00E3E5E0) and CAL_$SHUTDOWN (0x00E3E6E4)
 *
 * The real cal/write_timezone.c, cal/shutdown.c and cal/cal_data.c are
 * #included; PROC1 locks, DBUF and NETWORK are mocked and record their
 * calls.  The label buffer is a host bat_$label_t read back through the
 * same cal_$label_tz_t view the code writes with.
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
uint32_t TIME_$CLOCKH;

/* ==========================================================================
 * Mocked callees
 * ========================================================================== */

static int set_lock_calls, clr_lock_calls;
static int lock_held;

void (PROC1_$SET_LOCK)(uint32_t lock_id_slot)
{
    uint16_t lock_id = (uint16_t)ARCH_PASCAL_SLOT_WORD(lock_id_slot); (void)lock_id;
    if (lock_id != CAL_LOCK_ID) { tests_failed++; }
    set_lock_calls++;
    lock_held++;
}

void (PROC1_$CLR_LOCK)(uint32_t lock_id_slot)
{
    uint16_t lock_id = (uint16_t)ARCH_PASCAL_SLOT_WORD(lock_id_slot); (void)lock_id;
    if (lock_id != CAL_LOCK_ID) { tests_failed++; }
    clr_lock_calls++;
    lock_held--;
}

static bat_$label_t label;
static int get_block_calls;
static uint16_t gb_vol_idx;
static int32_t gb_block;
static uid_t *gb_uid;
static status_$t gb_status_value;
static status_$t *gb_status_ptr;

void *DBUF_$GET_BLOCK(uint16_t vol_idx, int32_t block, uid_t *uid,
                      uint32_t block_hint, uint16_t block_type,
                      uint16_t flags, status_$t *status)
{
    get_block_calls++;
    gb_vol_idx = vol_idx;
    gb_block = block;
    gb_uid = uid;
    gb_status_ptr = status;
    if (block_hint != 0 || block_type != 0 || flags != 0) { tests_failed++; }
    *status = gb_status_value;
    return &label;
}

static int set_buff_calls;
static void *sb_buffer;
static uint16_t sb_flags;
static status_$t sb_status_value;
static status_$t *sb_status_ptr;
static int lock_held_at_set;

void DBUF_$SET_BUFF(void *buffer, uint16_t flags, status_$t *status)
{
    set_buff_calls++;
    sb_buffer = buffer;
    sb_flags = flags;
    sb_status_ptr = status;
    lock_held_at_set = lock_held;
    *status = sb_status_value;
}

/* ==========================================================================
 * Code under test
 * ========================================================================== */

#include "../cal_data.c"
#include "../write_timezone.c"
#include "../shutdown.c"

static void reset(void)
{
    memset(&label, 0, sizeof(label));
    memset(&CAL_$TIMEZONE, 0, sizeof(CAL_$TIMEZONE));
    CAL_$BOOT_VOLX = 2;
    NETWORK_$DISKLESS = 0;
    TIME_$CLOCKH = 0xABCD1234;
    set_lock_calls = clr_lock_calls = 0;
    lock_held = 0;
    get_block_calls = set_buff_calls = 0;
    gb_status_value = status_$ok;
    sb_status_value = status_$ok;
}

static cal_$timezone_rec_t make_tz(const char *name, int16_t delta)
{
    cal_$timezone_rec_t tz;
    int i;

    tz.utc_delta = delta;
    for (i = 0; i < 4; i++) {
        tz.tz_name[i] = name[i];
    }
    tz.drift.high = 0x0DD1;
    tz.drift.low = 0x0DD2;
    return tz;
}

/* ==========================================================================
 * CAL_$WRITE_TIMEZONE
 * ========================================================================== */

TEST(write_installs_record_and_label)
{
    cal_$timezone_rec_t tz = make_tz("EST ", -300);
    status_$t status = 0x5555;
    cal_$label_tz_t *ltz = CAL_$LABEL_TZ(&label);

    reset();
    CAL_$WRITE_TIMEZONE(&tz, &status);

    ASSERT_EQ(status, status_$ok);
    ASSERT_EQ((uint16_t)CAL_$TIMEZONE.utc_delta, (uint16_t)-300);
    ASSERT_EQ(CAL_$TIMEZONE.tz_name[0], 'E');
    ASSERT_EQ(CAL_$TIMEZONE.drift.high, 0x0DD1);
    ASSERT_EQ(CAL_$TIMEZONE.drift.low, 0x0DD2);

    ASSERT_EQ(set_lock_calls, 1);
    ASSERT_EQ(clr_lock_calls, 1);
    ASSERT_EQ(lock_held, 0);
    ASSERT_EQ(get_block_calls, 1);
    ASSERT_EQ(gb_vol_idx, 2);
    ASSERT_EQ(gb_block, 0);
    ASSERT_TRUE(gb_uid == &LV_LABEL_$UID);

    ASSERT_EQ((uint16_t)ltz->utc_delta, (uint16_t)-300);
    ASSERT_EQ(ltz->tz_name[0], 'E');
    ASSERT_EQ(ltz->tz_name[3], ' ');
    ASSERT_EQ(label.mount_time_high, 0xABCD1234);
    ASSERT_EQ(ltz->last_valid_time, 0xABCD1234);

    ASSERT_EQ(set_buff_calls, 1);
    ASSERT_TRUE(sb_buffer == &label);
    ASSERT_EQ(sb_flags, 0x0B);
    ASSERT_EQ(lock_held_at_set, 1);
}

TEST(write_rejects_control_and_c1_characters)
{
    cal_$timezone_rec_t tz;
    status_$t status;

    reset();
    CAL_$TIMEZONE.utc_delta = 7;

    tz = make_tz("ES\x1fT", 60);
    status = 0;
    CAL_$WRITE_TIMEZONE(&tz, &status);
    ASSERT_EQ(status, status_$cal_date_or_time_invalid);
    ASSERT_EQ(status, 0x150002);

    tz = make_tz("EST\x7f", 60);          /* 0x7F is above 0x7E, at or below 0xA0 */
    status = 0;
    CAL_$WRITE_TIMEZONE(&tz, &status);
    ASSERT_EQ(status, status_$cal_date_or_time_invalid);

    tz = make_tz("\xa0" "EST", 60);        /* exactly 0xA0 is still rejected */
    status = 0;
    CAL_$WRITE_TIMEZONE(&tz, &status);
    ASSERT_EQ(status, status_$cal_date_or_time_invalid);

    /* nothing was installed or written */
    ASSERT_EQ(CAL_$TIMEZONE.utc_delta, 7);
    ASSERT_EQ(get_block_calls, 0);
    ASSERT_EQ(set_lock_calls, 0);
}

TEST(write_accepts_boundary_characters)
{
    cal_$timezone_rec_t tz = make_tz(" ~\xa1\xff", 0);
    status_$t status = 0x5555;

    reset();
    NETWORK_$DISKLESS = (int8_t)0xFF;
    CAL_$WRITE_TIMEZONE(&tz, &status);

    ASSERT_EQ(status, status_$ok);
    ASSERT_EQ((uint8_t)CAL_$TIMEZONE.tz_name[2], 0xA1);
    ASSERT_EQ((uint8_t)CAL_$TIMEZONE.tz_name[3], 0xFF);
    /* diskless: record installed, no disk traffic */
    ASSERT_EQ(get_block_calls, 0);
    ASSERT_EQ(set_lock_calls, 0);
}

TEST(write_get_block_failure)
{
    cal_$timezone_rec_t tz = make_tz("PST ", -480);
    status_$t status = 0;

    reset();
    gb_status_value = 0x00080005;
    CAL_$WRITE_TIMEZONE(&tz, &status);

    ASSERT_EQ(status, 0x00080005);
    ASSERT_EQ(set_lock_calls, 1);
    ASSERT_EQ(clr_lock_calls, 1);
    ASSERT_EQ(set_buff_calls, 0);
    /* the record was already installed before the disk step */
    ASSERT_EQ((uint16_t)CAL_$TIMEZONE.utc_delta, (uint16_t)-480);
}

TEST(write_set_buff_failure_is_reported)
{
    cal_$timezone_rec_t tz = make_tz("PST ", -480);
    status_$t status = 0;

    reset();
    sb_status_value = 0x00080009;
    CAL_$WRITE_TIMEZONE(&tz, &status);

    ASSERT_EQ(status, 0x00080009);
    ASSERT_EQ(clr_lock_calls, 1);
    ASSERT_EQ(lock_held, 0);
}

/* ==========================================================================
 * CAL_$SHUTDOWN
 * ========================================================================== */

TEST(shutdown_stamps_label)
{
    status_$t status = 0x5555;
    cal_$label_tz_t *ltz = CAL_$LABEL_TZ(&label);

    reset();
    CAL_$BOOT_VOLX = 5;
    TIME_$CLOCKH = 0x0BADF00D;

    CAL_$SHUTDOWN(&status);

    ASSERT_EQ(get_block_calls, 1);
    ASSERT_EQ(gb_vol_idx, 5);
    ASSERT_TRUE(gb_status_ptr == &status);
    ASSERT_EQ(set_lock_calls, 0);           /* no lock in SHUTDOWN */
    ASSERT_EQ(label.mount_time_high, 0x0BADF00D);
    ASSERT_EQ(ltz->last_valid_time, 0x0BADF00D);
    ASSERT_EQ(set_buff_calls, 1);
    ASSERT_EQ(sb_flags, 0x0B);
    ASSERT_TRUE(sb_status_ptr == &status);
    ASSERT_EQ(status, status_$ok);
}

TEST(shutdown_get_block_failure)
{
    status_$t status = 0;

    reset();
    gb_status_value = 0x00080005;
    CAL_$SHUTDOWN(&status);

    ASSERT_EQ(status, 0x00080005);
    ASSERT_EQ(set_buff_calls, 0);
    ASSERT_EQ(label.mount_time_high, 0);
}

int main(void)
{
    printf("test_write_timezone:\n");

    RUN_TEST(write_installs_record_and_label);
    RUN_TEST(write_rejects_control_and_c1_characters);
    RUN_TEST(write_accepts_boundary_characters);
    RUN_TEST(write_get_block_failure);
    RUN_TEST(write_set_buff_failure_is_reported);
    RUN_TEST(shutdown_stamps_label);
    RUN_TEST(shutdown_get_block_failure);

    printf("\n=== Results: %d passed, %d failed ===\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
