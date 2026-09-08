/*
 * audit/test/test_log_event_s.c - unit tests for AUDIT_$LOG_EVENT_S
 * (0x00E70E40).
 *
 * audit/log_event_s.c is #included below with everything it calls mocked.
 * The tests pin down bead source-zkeb: PROC2_$GET_MY_UPIDS' argument order
 * (the image pushes (0x42,A3), (0x44,A3), (0x40,A3), so argument 2 is the
 * record's 0x44 word and argument 3 is its 0x42 word) and the UID_$HASH
 * modulus being the code-segment cell at 0x00E710C4.
 */

#include <stdio.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;
static int current_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do {                     \
    printf("  Running %s... ", #name);          \
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

#define ASSERT_TRUE(cond) do {                                           \
    if (!(cond)) {                                                       \
        printf("FAILED\n    %s at line %d\n", #cond, __LINE__);          \
        current_failed = 1;                                              \
        return;                                                          \
    }                                                                    \
} while (0)

#include "audit/audit_internal.h"
#include "acl/acl.h"
#include "time/time.h"
#include "os/os.h"
#include "file/file.h"
#include "mst/mst.h"
#include "misc/crash_system.h"

/* ------------------------------------------------------------------ */

audit_data_t AUDIT_$DATA;
int8_t       AUDIT_$ENABLED;
int8_t       AUDIT_$CORRUPTED;
uint16_t     PROC1_$CURRENT;
uint16_t     PROC1_$AS_ID;
uid_t        UID_$NIL = { 0, 0 };
uint32_t     NODE_$ME;

/* ------------------------------------------------------------------ */
/* Mocks                                                               */
/* ------------------------------------------------------------------ */

void ML_$EXCLUSION_START(ml_$exclusion_t *e) { (void)e; }
void ML_$EXCLUSION_STOP(ml_$exclusion_t *e)  { (void)e; }
void ACL_$ENTER_SUPER(void) { }
void ACL_$EXIT_SUPER(void)  { }

static int              hash_calls;
static const uint16_t  *hash_modulus_arg;
static uint32_t         hash_result;

uint32_t UID_$HASH(uid_t *uid, uint16_t *table_size)
{
    hash_calls++;
    hash_modulus_arg = table_size;
    (void)uid;
    return hash_result;
}

void TIME_$CLOCK(clock_t *out) { memset(out, 0x5A, sizeof(*out)); }

/*
 * PROC2_$GET_MY_UPIDS(upid, upgid, uppid) - each output gets a value that
 * says which PARAMETER it came from, so the caller's argument order shows up
 * in the record.
 */
static int       upids_calls;
static uint16_t *upids_arg1;
static uint16_t *upids_arg2;
static uint16_t *upids_arg3;

void PROC2_$GET_MY_UPIDS(uint16_t *upid, uint16_t *upgid, uint16_t *uppid)
{
    upids_calls++;
    upids_arg1 = upid;
    upids_arg2 = upgid;
    upids_arg3 = uppid;
    *upid  = 0x1111;   /* argument 1 */
    *upgid = 0x2222;   /* argument 2 */
    *uppid = 0x3333;   /* argument 3 */
}

void OS_$DATA_COPY(const void *src, void *dst, uint32_t len)
{
    memcpy(dst, src, (size_t)len);
}

void FILE_$FW_FILE(uid_t *u, status_$t *s) { (void)u; *s = status_$ok; }

void MST_$UNMAP_PRIVI(int16_t mode, uid_t *uid, uint32_t start, uint32_t size,
                      uint16_t asid, status_$t *status_ret)
{
    (void)mode; (void)uid; (void)start; (void)size; (void)asid;
    *status_ret = status_$ok;
}

static uint8_t buffer[512];

void *MST_$MAPS(int16_t asid, boolean direction, uid_t *uid, uint32_t start_va,
                uint32_t length, int16_t area_id, uint32_t area_size,
                boolean access_rights, void *map_info, status_$t *status)
{
    (void)asid; (void)direction; (void)uid; (void)start_va; (void)length;
    (void)area_id; (void)area_size; (void)access_rights;
    *(uint32_t *)map_info = sizeof(buffer);
    *status = status_$ok;
    return buffer;
}

void audit_$close_log(status_$t *s) { *s = status_$ok; }
void audit_$open_log(status_$t *s)  { *s = status_$ok; }

static int crash_calls;
void CRASH_SYSTEM(const status_$t *s) { crash_calls++; (void)s; }

#include "../log_event_s.c"

/* ------------------------------------------------------------------ */

#define TEST_PID 2

static uint8_t   wired[64];
static uid_t     ev_uid = { 0x00040007u, 0 };
static uint16_t  ev_flag;
static uint8_t   ev_sid[36];
static status_$t ev_status;
static char      ev_data[16];
static uint16_t  ev_len;

static void call_log(uint16_t flags, int16_t list_count, uint16_t asid,
                     uint16_t data_len)
{
    unsigned i;

    memset(&AUDIT_$DATA, 0, sizeof(AUDIT_$DATA));
    memset(buffer, 0, sizeof(buffer));
    AUDIT_$DATA.event_count = (ec_$eventcount_t *)wired;
    AUDIT_$DATA.buffer_base = buffer;
    AUDIT_$DATA.write_ptr = buffer;
    AUDIT_$DATA.buffer_size = sizeof(buffer);
    AUDIT_$DATA.bytes_remaining = sizeof(buffer);
    AUDIT_$DATA.flags = flags;
    AUDIT_$DATA.list_count = list_count;

    AUDIT_$ENABLED = (int8_t)0xFF;
    AUDIT_$CORRUPTED = 0;
    PROC1_$CURRENT = TEST_PID;
    PROC1_$AS_ID = asid;
    NODE_$ME = 0x12345;

    for (i = 0; i < sizeof(ev_sid); i++) {
        ev_sid[i] = (uint8_t)(0x40 + i);
    }
    ev_flag = 0x0007;
    ev_status = 0x00230001;
    memcpy(ev_data, "abcdefgh", 9);
    ev_len = data_len;

    hash_calls = 0;
    hash_modulus_arg = NULL;
    hash_result = 0;
    upids_calls = 0;
    upids_arg1 = upids_arg2 = upids_arg3 = NULL;
    crash_calls = 0;

    AUDIT_$LOG_EVENT_S(&ev_uid, &ev_flag, ev_sid, &ev_status, ev_data, &ev_len);
}

/* 0x00E710C4: the word 0x0025 = 37. */
TEST(hash_modulus_cell)
{
    ASSERT_EQ(37, audit_$hash_modulus);
    ASSERT_EQ(2, sizeof(audit_$hash_modulus));
    /* The same constant audit_$add_to_hash reaches with pea (-0x228,PC). */
    ASSERT_EQ(AUDIT_HASH_TABLE_SIZE, audit_$hash_modulus);
}

/* 0x00E70EB4: UID_$HASH's second argument is that cell's address. */
TEST(uid_hash_gets_the_code_segment_cell)
{
    audit_hash_node_t node;

    memset(&node, 0, sizeof(node));
    node.uid_high = ev_uid.high;
    node.uid_low  = ev_uid.low;
    node.next = NULL;

    /* flags bit 0 clear + CORRUPTED clear + a non-empty list => hash path. */
    call_log(0x0000, 1, 1, 8);
    /* The bucket the mock returned is 0, and bucket 0 was NULL, so nothing
     * was logged - but UID_$HASH was still consulted with the right cell. */
    ASSERT_EQ(1, hash_calls);
    ASSERT_TRUE(hash_modulus_arg == (const uint16_t *)&audit_$hash_modulus);
}

/* 0x00E70E98-0x00E70EA6: flags bit 0 set logs everything, no hash lookup. */
TEST(selective_flag_skips_the_hash)
{
    call_log(AUDIT_FLAG_SELECTIVE, 0, 1, 8);
    ASSERT_EQ(0, hash_calls);
    ASSERT_EQ(1, upids_calls);
}

/*
 * 0x00E71012-0x00E7101A.  Argument 1 (upid) is record+0x40, argument 2
 * (upgid) is record+0x44 and argument 3 (uppid) is record+0x42.
 */
TEST(get_my_upids_argument_order)
{
    audit_event_record_t *rec = (audit_event_record_t *)buffer;

    /*
     * Compare the POINTERS the callee received, not the bytes: the record is
     * deliberately not `packed`, so on a 4-byte-aligning host its members do
     * not sit at the m68k offsets (those are asserted in
     * audit/audit_internal.h under ARCH_M68K) and the raw `+0x46` data copy
     * would land on top of them.
     */
    call_log(AUDIT_FLAG_SELECTIVE, 0, 1, 8);

    ASSERT_EQ(1, upids_calls);
    ASSERT_TRUE(upids_arg1 == (uint16_t *)&rec->upid);   /* m68k +0x40 */
    ASSERT_TRUE(upids_arg2 == (uint16_t *)&rec->upgid);  /* m68k +0x44 */
    ASSERT_TRUE(upids_arg3 == (uint16_t *)&rec->uppid);  /* m68k +0x42 */
}

/* 0x00E7102A-0x00E71034: with no ASID, upid = PROC1_$CURRENT, the rest 0. */
TEST(no_asid_branch)
{
    const audit_event_record_t *rec = (const audit_event_record_t *)buffer;

    /* No data, so the raw +0x46 terminator cannot disturb the host layout. */
    call_log(AUDIT_FLAG_SELECTIVE, 0, 0, 0);

    ASSERT_EQ(0, upids_calls);
    ASSERT_EQ(TEST_PID, rec->upid);
    ASSERT_EQ(0, rec->uppid);
    ASSERT_EQ(0, rec->upgid);
}

/* 0x00E70EF4-0x00E70F02 and 0x00E7103C-0x00E7104E. */
TEST(record_header_and_data)
{
    const audit_event_record_t *rec = (const audit_event_record_t *)buffer;
    uint16_t expected;

    call_log(AUDIT_FLAG_SELECTIVE, 0, 1, 8);

    expected = (uint16_t)(0x47 + 8);
    if (expected & 1) {
        expected++;
    }
    ASSERT_EQ(expected, rec->record_size);
    ASSERT_EQ(1, rec->version);
    ASSERT_EQ(0x0007, rec->event_flags);
    ASSERT_EQ(0x00040007u, rec->event_uid.high);
    ASSERT_EQ(0x00230001u, rec->status);
    ASSERT_EQ(0, memcmp(rec->sid_data, ev_sid, 36));
    (void)0;
    ASSERT_EQ(0, memcmp(buffer + 0x46, "abcdefgh", 8));
    ASSERT_EQ(0, buffer[0x46 + 8]);
    ASSERT_EQ((int8_t)-1, AUDIT_$DATA.dirty);
    ASSERT_EQ(0, crash_calls);
    /* 0x00E710B0: the suspend counter is put back. */
    ASSERT_EQ(0, AUDIT_$DATA.suspend_count[TEST_PID - 1]);
}

/* 0x00E70E56: auditing off is an immediate return. */
TEST(disabled_does_nothing)
{
    call_log(AUDIT_FLAG_SELECTIVE, 0, 1, 8);
    memset(buffer, 0, sizeof(buffer));
    AUDIT_$ENABLED = 0;
    upids_calls = 0;

    AUDIT_$LOG_EVENT_S(&ev_uid, &ev_flag, ev_sid, &ev_status, ev_data, &ev_len);

    ASSERT_EQ(0, upids_calls);
    ASSERT_EQ(0, buffer[0]);
}

int main(void)
{
    printf("AUDIT_$LOG_EVENT_S tests\n");

    RUN_TEST(hash_modulus_cell);
    RUN_TEST(uid_hash_gets_the_code_segment_cell);
    RUN_TEST(selective_flag_skips_the_hash);
    RUN_TEST(get_my_upids_argument_order);
    RUN_TEST(no_asid_branch);
    RUN_TEST(record_header_and_data);
    RUN_TEST(disabled_does_nothing);

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
