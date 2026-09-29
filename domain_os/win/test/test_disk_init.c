/*
 * win/test/test_disk_init.c - unit tests for DISK_INIT (0x00E19986)
 *
 * The real win/disk_init.c is #included below and driven through mocks for
 * WAIT_FOR_CONTROLLER, WIN_$ANSI_COMMAND, WIN_$CHECK_DISK_STATUS, EC_$WAIT
 * and the two Domain Pascal multiply helpers.
 *
 * Coverage: the sub-unit refusal, the ten-attempt retry loop and its
 * fall-out, the spin-up branch and its EC_$WAIT operands, the reported drive
 * identifier, the "caller already has geometry" short circuit, all three
 * recognised drives, and the unrecognised-drive path that still writes a
 * product into *total_blocks.
 */

#include <stdio.h>
#include <string.h>

#include "win/win_internal.h"

/* ------------------------------------------------------------------ */
/* Module data and the globals DISK_INIT reaches                       */
/* ------------------------------------------------------------------ */
MODULE_DATA_DEFINE(win_$data_t, WIN_$DATA, 0x00E2B89C);
uint32_t TIME_$CLOCKH;

uint32_t win_$host_clockh(void) { return TIME_$CLOCKH; }

/* ------------------------------------------------------------------ */
/* Mocks                                                               */
/* ------------------------------------------------------------------ */
#define MAX_CALLS 64

static int ansi_calls;
static uint16_t ansi_cmd_log[MAX_CALLS];
static char ansi_in_log[MAX_CALLS];
static status_$t ansi_status;      /* what WIN_$ANSI_COMMAND returns */
static char ansi_general_status;   /* what a < 0x40 command hands back */
static char ansi_attribute;        /* what LOAD/REPORT ATTRIBUTE hands back */

static int check_calls;
static status_$t check_status_seq[MAX_CALLS];

static int wait_calls;

static int ec_wait_calls;
static ec_$wait_ecs_t ec_wait_ecs;
static ec_$wait_vals_t ec_wait_vals;

status_$t WAIT_FOR_CONTROLLER(uint16_t unit)
{
    (void)unit;
    wait_calls++;
    return status_$ok;
}

status_$t WIN_$ANSI_COMMAND(uint16_t unit, uint16_t ansi_cmd,
                            char *ansi_in_param, char *ansi_out_param)
{
    (void)unit;
    if (ansi_calls < MAX_CALLS) {
        ansi_cmd_log[ansi_calls] = ansi_cmd;
        ansi_in_log[ansi_calls] = ansi_in_param ? *ansi_in_param : 0;
    }
    ansi_calls++;

    if (ansi_cmd == ANSI_CMD_LOAD_ATTRIBUTE_NUMBER ||
        ansi_cmd == ANSI_CMD_REPORT_DRIVE_ATTRIBUTE) {
        *ansi_out_param = ansi_attribute;
    } else {
        *ansi_out_param = ansi_general_status;
    }
    return ansi_status;
}

status_$t WIN_$CHECK_DISK_STATUS(uint16_t unit)
{
    (void)unit;
    if (check_calls < MAX_CALLS) {
        return check_status_seq[check_calls++];
    }
    check_calls++;
    return status_$ok;
}

int16_t EC_$WAIT(ec_$wait_ecs_t ecs, ec_$wait_vals_t vals)
{
    ec_wait_calls++;
    ec_wait_ecs = ecs;
    ec_wait_vals = vals;
    return 0;
}

/* The real helpers are plain signed truncating multiplies (0x00E0AC44 and
 * 0x00E0ABD4); reproduce that, not a checked one. */
long M$MIS$LLW(long multiplicand, short multiplier)
{
    return (long)((int32_t)multiplicand * (int32_t)multiplier);
}

long M$MIS$LLL(long multiplicand, long multiplier)
{
    return (long)((int32_t)multiplicand * (int32_t)multiplier);
}

#include "../disk_init.c"

/* ------------------------------------------------------------------ */
/* Tiny test harness                                                   */
/* ------------------------------------------------------------------ */
static int tests_run;
static int tests_failed;
static int current_failed;

#define CHECK_EQ(expected, actual)                                             \
    do {                                                                       \
        long long e_ = (long long)(expected);                                  \
        long long a_ = (long long)(actual);                                    \
        if (e_ != a_) {                                                        \
            current_failed = 1;                                                \
            printf("\n    FAIL line %d: %s: expected 0x%llx, got 0x%llx",      \
                   __LINE__, #actual, (unsigned long long)e_,                  \
                   (unsigned long long)a_);                                    \
        }                                                                      \
    } while (0)

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            current_failed = 1;                                                \
            printf("\n    FAIL line %d: %s", __LINE__, #cond);                 \
        }                                                                      \
    } while (0)

#define RUN(fn)                                                                \
    do {                                                                       \
        tests_run++;                                                           \
        current_failed = 0;                                                    \
        printf("  %-40s", #fn);                                                \
        reset_state();                                                         \
        fn();                                                                  \
        if (current_failed) {                                                  \
            tests_failed++;                                                    \
            printf("\n  %-40s FAILED\n", #fn);                                 \
        } else {                                                               \
            printf(" ok\n");                                                   \
        }                                                                      \
    } while (0)

/* The drive's register block; unit 0's record points at it. */
static uint8_t regs[0x10];

static void reset_state(void)
{
    int i;

    memset(WIN_$DATA.bytes, 0, sizeof(WIN_$DATA.bytes));
    memset(regs, 0, sizeof(regs));
    *(uint8_t **)(WIN_UNIT(0) + WIN_BASE_ADDR_OFFSET) = regs;

    TIME_$CLOCKH = 0;
    ansi_calls = 0;
    ansi_status = status_$ok;
    ansi_general_status = 0;
    ansi_attribute = 0;
    check_calls = 0;
    for (i = 0; i < MAX_CALLS; i++) {
        check_status_seq[i] = status_$ok;
    }
    wait_calls = 0;
    ec_wait_calls = 0;
}

/* Drive DISK_INIT with a fresh set of output cells. */
struct out {
    int32_t total_blocks;
    uint16_t blocks_per_track;
    uint16_t heads;
    uint16_t geometry[2];
    uint16_t drive_id;
};

static status_$t call_init(uint16_t sub_unit, struct out *o)
{
    return DISK_INIT(0, sub_unit, &o->total_blocks, &o->blocks_per_track,
                     &o->heads, o->geometry, &o->drive_id);
}

/* ------------------------------------------------------------------ */
/* Tests                                                               */
/* ------------------------------------------------------------------ */

/* 0x00E199A4-0x00E199AE: any sub-unit but 0 is refused before anything runs */
static void test_sub_unit_refused(void)
{
    struct out o;

    memset(&o, 0, sizeof(o));
    WIN_CUR_REQ = (void *)0x1234;

    CHECK_EQ(status_$invalid_unit_number, call_init(1, &o));
    CHECK_EQ(0, ansi_calls);
    CHECK_EQ(0, wait_calls);
    /* the refusal happens before `clr.l (0x60,A5)` */
    CHECK(WIN_CUR_REQ == (void *)0x1234);
}

/*
 * The happy path with the spindle already turning: one attempt, two ANSI
 * commands (general status then write control), no EC_$WAIT.
 */
static void test_already_spinning(void)
{
    struct out o;

    memset(&o, 0, sizeof(o));
    o.total_blocks = 100; /* > 0: the caller already knows the geometry */
    ansi_general_status = 0; /* bit 0 clear -> spindle running */
    ansi_attribute = (char)WIN_DRIVE_PRIAM_3450;
    WIN_CUR_REQ = (void *)0x1234;

    CHECK_EQ(status_$ok, call_init(0, &o));
    CHECK(WIN_CUR_REQ == NULL);        /* 0x00E199B2 */
    CHECK_EQ(1, wait_calls);
    CHECK_EQ(0, ec_wait_calls);
    CHECK_EQ(1, regs[WIN_REG_PARAM]);  /* 0x00E199DA */
    CHECK_EQ(6, regs[WIN_REG_GO]);     /* 0x00E199E4 */

    /* general status, write control, load attribute, report attribute */
    CHECK_EQ(4, ansi_calls);
    CHECK_EQ(ANSI_CMD_REPORT_GENERAL_STATUS, ansi_cmd_log[0]);
    CHECK_EQ(ANSI_CMD_WRITE_CONTROL, ansi_cmd_log[1]);
    CHECK_EQ((char)0x80, ansi_in_log[1]); /* the 0x00E19BBC cell */
    CHECK_EQ(ANSI_CMD_LOAD_ATTRIBUTE_NUMBER, ansi_cmd_log[2]);
    CHECK_EQ(0x02, ansi_in_log[2]);       /* the 0x00E19BBE cell */
    CHECK_EQ(ANSI_CMD_REPORT_DRIVE_ATTRIBUTE, ansi_cmd_log[3]);

    /* 0x00E19B08: the reported id is 0x100 + the low nibble */
    CHECK_EQ(0x0104, o.drive_id);

    /* 0x00E19B18: *total_blocks > 0 short circuits the geometry table */
    CHECK_EQ(100, o.total_blocks);
    CHECK_EQ(0, o.blocks_per_track);
    CHECK_EQ(0, o.geometry[0]);
}

/*
 * 0x00E19A1C-0x00E19A7C: bit 0 of the general status spins the platter up,
 * and the wait uses the unit's eventcount plus TIME_$CLOCKH as a timeout.
 */
static void test_spin_up(void)
{
    struct out o;
    ec_$eventcount_t *ec = WIN_UNIT_EC(0);

    memset(&o, 0, sizeof(o));
    o.total_blocks = 1;
    ansi_general_status = 0x01;
    ansi_attribute = (char)WIN_DRIVE_MICROPOLIS_1203;
    ec->value = 41;
    TIME_$CLOCKH = 1000;

    CHECK_EQ(status_$ok, call_init(0, &o));
    CHECK_EQ(0x0A, regs[WIN_REG_MODE]); /* 0x00E19A28 */
    CHECK_EQ(1, ec_wait_calls);

    /* ecs = { &WIN_EC[unit], &TIME_$CLOCKH, NULL } */
    CHECK(ec_wait_ecs.ec[0] == ec);
    CHECK(ec_wait_ecs.ec[1] == (ec_$eventcount_t *)&TIME_$CLOCKH);
    CHECK(ec_wait_ecs.ec[2] == NULL);

    /* vals = { ec->value + 1, TIME_$CLOCKH + 0x78, 0 } */
    CHECK_EQ(42, ec_wait_vals.val[0]);
    CHECK_EQ(1000 + 0x78, ec_wait_vals.val[1]);
    CHECK_EQ(0, ec_wait_vals.val[2]);

    /* general status, spin control, write control, and the two attributes */
    CHECK_EQ(ANSI_CMD_SPIN_CONTROL, ansi_cmd_log[1]);
    CHECK_EQ((char)0x80, ansi_in_log[1]);
    CHECK_EQ(ANSI_CMD_WRITE_CONTROL, ansi_cmd_log[2]);
}

/*
 * 0x00E199C8 / 0x00E19AA2: ten attempts.  A failure that persists returns
 * the last status after exactly ten passes.
 */
static void test_retries_exhausted(void)
{
    struct out o;

    memset(&o, 0, sizeof(o));
    ansi_status = status_$disk_not_ready;

    CHECK_EQ(status_$disk_not_ready, call_init(0, &o));
    /* one general-status command per attempt */
    CHECK_EQ(10, ansi_calls);
    CHECK_EQ(10, wait_calls);
    /* the drive id is never read, so nothing after the loop ran */
    CHECK_EQ(0, o.drive_id);
}

/* A failure that clears on the third attempt is retried, then proceeds. */
static void test_retry_then_success(void)
{
    struct out o;

    memset(&o, 0, sizeof(o));
    o.total_blocks = 1;
    ansi_attribute = (char)WIN_DRIVE_PRIAM_7050;
    /*
     * The write-control check is the second WIN_$CHECK_DISK_STATUS of each
     * attempt (the first one's result is discarded at 0x00E19A0C).
     */
    check_status_seq[1] = status_$disk_seek_error;
    check_status_seq[3] = status_$disk_seek_error;

    CHECK_EQ(status_$ok, call_init(0, &o));
    CHECK_EQ(3, wait_calls);
    CHECK_EQ(0x0105, o.drive_id);
}

/* 0x00E19B22-0x00E19BAE: the geometry table for the three known drives. */
static void test_geometry_priam_3450(void)
{
    struct out o;

    memset(&o, 0, sizeof(o));
    ansi_attribute = (char)WIN_DRIVE_PRIAM_3450;

    CHECK_EQ(status_$ok, call_init(0, &o));
    CHECK_EQ(0x0104, o.drive_id);
    CHECK_EQ(12, o.blocks_per_track);
    CHECK_EQ(5, o.heads);
    CHECK_EQ(0, o.geometry[0]);
    CHECK_EQ(1120, o.geometry[1]);
    CHECK_EQ(5 * 525 * 12, o.total_blocks);
}

static void test_geometry_micropolis_1203(void)
{
    struct out o;

    memset(&o, 0, sizeof(o));
    ansi_attribute = (char)WIN_DRIVE_MICROPOLIS_1203;

    CHECK_EQ(status_$ok, call_init(0, &o));
    CHECK_EQ(0x0103, o.drive_id);
    CHECK_EQ(12, o.blocks_per_track);
    CHECK_EQ(5, o.heads);
    CHECK_EQ(1181, o.geometry[1]);
    CHECK_EQ(5 * 525 * 12, o.total_blocks);
}

static void test_geometry_priam_7050(void)
{
    struct out o;

    memset(&o, 0, sizeof(o));
    ansi_attribute = (char)WIN_DRIVE_PRIAM_7050;

    CHECK_EQ(status_$ok, call_init(0, &o));
    CHECK_EQ(0x0105, o.drive_id);
    CHECK_EQ(12, o.blocks_per_track);
    CHECK_EQ(5, o.heads);
    /* PRIAM 7050 falls into the 0x00E19B78 store, so it gets 1120 too */
    CHECK_EQ(1120, o.geometry[1]);
    CHECK_EQ(5 * 1049 * 12, o.total_blocks);
}

/*
 * 0x00E19B80: an unrecognised drive sets the error status and then FALLS
 * THROUGH into the multiply with D1 still holding the drive identifier and
 * *heads / *blocks_per_track untouched, overwriting *total_blocks anyway.
 */
static void test_unrecognised_drive(void)
{
    struct out o;

    memset(&o, 0, sizeof(o));
    o.heads = 7;
    o.blocks_per_track = 3;
    ansi_attribute = (char)0x09; /* not 3, 4 or 5 */

    CHECK_EQ(status_$unrecognized_drive_id, call_init(0, &o));
    CHECK_EQ(0x0109, o.drive_id);
    /* geometry[0] was cleared, geometry[1] never written */
    CHECK_EQ(0, o.geometry[0]);
    CHECK_EQ(0, o.geometry[1]);
    /* the caller's heads/bpt survive, and the product uses the drive id */
    CHECK_EQ(7, o.heads);
    CHECK_EQ(3, o.blocks_per_track);
    CHECK_EQ(7 * 9 * 3, o.total_blocks);
}

int main(void)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    printf("DISK_INIT (0x00E19986) tests\n");

    RUN(test_sub_unit_refused);
    RUN(test_already_spinning);
    RUN(test_spin_up);
    RUN(test_retries_exhausted);
    RUN(test_retry_then_success);
    RUN(test_geometry_priam_3450);
    RUN(test_geometry_micropolis_1203);
    RUN(test_geometry_priam_7050);
    RUN(test_unrecognised_drive);

    printf("%d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed != 0;
}
