/*
 * dir/test/test_get_entryu_fun.c - Request-build tests for
 * DIR_$GET_ENTRYU_FUN_00e4d460 (0x00E4D460)
 *
 * source-qwjg: the C used to read DIR_$OP_TAB (0xE7FC42) for both the word it
 * stamps into the request and the request-length delta, and passed 0x1c as
 * DIR_$DO_OP's resp_size.  The assembly does none of that.  With A5 = 0xE7DC00
 * (established by the parent, DIR_$GET_ENTRYU, at 0x00E4D508) the two words are
 * separate cells in the GET_ENTRYU record of the DIR operation parameter table
 * (DIR_$OP_TAB record 13, not record 0):
 *
 *   0x00E4D4A6  move.w (0x20aa,A5),(-0x1aa,A6)   0xE7FCAA -> request+0x0E
 *   0x00E4D4B4  move.w #0x22,-(SP)               resp_size = 0x22, not 0x1c
 *   0x00E4D4B8  move.w (0x20ae,A5),D0w           0xE7FCAE, added to name_len
 *   0x00E4D4BC  add.w  (-0x12a,A6),D0w
 *   0x00E4D4C2  pea (-0x1b8,A6)                  request base (op byte at +3)
 *
 * These tests drive the routine through a mocked DIR_$DO_OP that snapshots the
 * request bytes and the two word arguments.
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

#include "dir/dir_internal.h"

/*
 * The DIR block: the two cells the routine reads through A5 are the
 * GET_ENTRYU record of DIR_$OP_TAB (op 0x44 >> 1 = 0x22, record 13 at
 * 0xE7FCAA), whose image words are version 0x0000 and base_size 0x0002.
 */
MODULE_DATA_DEFINE(dir_$data_t, DIR_$DATA, 0x00E7DBF8);
#define GET_ENTRYU_REC DIR_$OP_REC(DIR_OP_GET_ENTRYU_OP >> 1)

/* ------------------------------------------------------------------ */
/* Mocked DIR_$DO_OP                                                     */
/* ------------------------------------------------------------------ */

#define REQ_SNAP_LEN    0x180

static int       mock_calls;
static uint8_t   mock_request[REQ_SNAP_LEN];
static int16_t   mock_req_size;
static int16_t   mock_resp_size;
static int       mock_received_len_written;
static uint8_t   mock_reply[sizeof(Dir_$OpResponse)];

void DIR_$DO_OP(void *request, int16_t req_size, int16_t resp_size,
                void *response, uint16_t *received_len)
{
    mock_calls++;
    memcpy(mock_request, request, sizeof(mock_request));
    mock_req_size = req_size;
    mock_resp_size = resp_size;
    if (received_len != NULL) {
        *received_len = (uint16_t)resp_size;
        mock_received_len_written = 1;
    }
    memcpy(response, mock_reply, sizeof(mock_reply));
}

#include "../get_entryu_fun.c"

/* Payload base of Dir_$OpResponse; status_$t is wider on a 64-bit host, so the
 * payload offsets are taken relative to it (see source-39bo). */
#define PL(off)  (__builtin_offsetof(Dir_$OpResponse, uid) + (off))

/* The request is built with native stores, so read it back natively rather
 * than assuming the m68k byte order of the host running the test. */
static uint16_t rd16(size_t off)
{
    uint16_t v;
    memcpy(&v, mock_request + off, sizeof(v));
    return v;
}

static uint32_t rd32(size_t off)
{
    uint32_t v;
    memcpy(&v, mock_request + off, sizeof(v));
    return v;
}

static uint32_t entry32(const uint8_t *entry, size_t off)
{
    uint32_t v;
    memcpy(&v, entry + off, sizeof(v));
    return v;
}

static void put32(uint8_t *b, size_t off, uint32_t v)
{
    memcpy(b + off, &v, 4);
}

/*
 * The routine's request buffer is an uninitialised 0x18F-byte stack frame, so
 * "this field was never stored to" can only be observed by pre-filling the
 * stack the frame will occupy.  scrub_stack() writes 0xCC over a region larger
 * than the frame and returns, leaving the pattern behind for the next call.
 */
static void scrub_stack(void)
{
    volatile uint8_t pad[0x400];
    memset((void *)pad, 0xCC, sizeof(pad));
}

static void reset_mock(void)
{
    mock_calls = 0;
    mock_req_size = 0;
    mock_resp_size = 0;
    mock_received_len_written = 0;
    memset(mock_request, 0xCC, sizeof(mock_request));
    memset(mock_reply, 0, sizeof(mock_reply));
}

/* ------------------------------------------------------------------ */
/* Tests                                                                */
/* ------------------------------------------------------------------ */

/*
 * The request the assembly builds at A6-0x1B8: op byte at +0x03, directory UID
 * at +0x04, the table parm word at +0x0E, the name length at +0x8E and the
 * name bytes at +0x90.
 */
TEST(request_bytes_match_the_stores)
{
    uid_t dir_uid = { 0x11223344, 0x55667788 };
    uint8_t entry[16];
    status_$t status = 0xdeadbeef;
    char name[8] = "abcdef";
    size_t i;

    reset_mock();
    GET_ENTRYU_REC.version = 0x1357;
    GET_ENTRYU_REC.base_size = 0x0002;
    memset(entry, 0, sizeof(entry));

    scrub_stack();
    DIR_$GET_ENTRYU_FUN_00e4d460(&dir_uid, name, 6, entry, &status);

    ASSERT_EQ(1, mock_calls);
    /* 0x00E4D494: move.b #0x44,(-0x1b5,A6) - op byte is at request+3. */
    ASSERT_EQ(0x44, mock_request[0x03]);
    ASSERT_EQ(DIR_OP_GET_ENTRYU_OP, mock_request[0x03]);
    /* 0x00E4D49E/0x00E4D4A2: the two longwords of the UID at request+4. */
    ASSERT_EQ(0x11223344, rd32(0x04));
    ASSERT_EQ(0x55667788, rd32(0x08));
    /* 0x00E4D4A6: (0x20aa,A5) = 0xE7FCAA lands at request+0x0E, not +0x0C. */
    ASSERT_EQ(0x1357, rd16(0x0E));
    /* 0x00E4D472: the length word at request+0x8E. */
    ASSERT_EQ(6, rd16(0x8E));
    /* 0x00E4D488: the name bytes start at request+0x90. */
    for (i = 0; i < 6; i++) {
        ASSERT_EQ((unsigned char)name[i], mock_request[0x90 + i]);
    }
    /* Nothing is written between the parm word and the length word. */
    for (i = 0x10; i < 0x8E; i++) {
        ASSERT_EQ(0xCC, mock_request[i]);
    }
    ASSERT_EQ(0xCC, mock_request[0x0C]);
    ASSERT_EQ(0xCC, mock_request[0x0D]);
    /* The name bytes stop where the name does. */
    ASSERT_EQ(0xCC, mock_request[0x90 + 6]);
}

/*
 * req_size = name_len + the record's base_size (0x00E4D4B8..0x00E4D4C0) and
 * resp_size = 0x22 (0x00E4D4B4).  The old code used record 0 of DIR_$OP_TAB
 * and 0x1c.
 */
TEST(length_and_response_size_arguments)
{
    uid_t dir_uid = { 0, 0 };
    uint8_t entry[16];
    status_$t status = 0;
    char name[4] = "xy";

    reset_mock();
    GET_ENTRYU_REC.version = 0x0000;
    GET_ENTRYU_REC.base_size = 0x0002;   /* the value in the image, 0xE7FCAE */

    scrub_stack();
    DIR_$GET_ENTRYU_FUN_00e4d460(&dir_uid, name, 2, entry, &status);
    ASSERT_EQ(2 + 2, mock_req_size);
    ASSERT_EQ(0x22, mock_resp_size);
    ASSERT_EQ(1, mock_received_len_written);

    /* The delta really comes from the cell, not from a constant. */
    reset_mock();
    GET_ENTRYU_REC.base_size = 0x0007;
    scrub_stack();
    DIR_$GET_ENTRYU_FUN_00e4d460(&dir_uid, name, 200, entry, &status);
    ASSERT_EQ(200 + 7, mock_req_size);
    ASSERT_EQ(0x22, mock_resp_size);

    /*
     * DIR_$DO_OP adds 0x8E to req_size (0x00E4C110), so with the image value
     * the wire length is exactly through the end of the name.
     */
    reset_mock();
    GET_ENTRYU_REC.base_size = 0x0002;
    scrub_stack();
    DIR_$GET_ENTRYU_FUN_00e4d460(&dir_uid, name, 2, entry, &status);
    ASSERT_EQ(0x90 + 2, 0x8E + mock_req_size);
}

/* A zero-length name copies nothing (0x00E4D47A subq/bmi skips the dbf loop). */
TEST(zero_length_name_copies_nothing)
{
    uid_t dir_uid = { 0, 0 };
    uint8_t entry[16];
    status_$t status = 0;
    char name[4] = "zz";

    reset_mock();
    GET_ENTRYU_REC.version = 0;
    GET_ENTRYU_REC.base_size = 2;

    scrub_stack();
    DIR_$GET_ENTRYU_FUN_00e4d460(&dir_uid, name, 0, entry, &status);
    ASSERT_EQ(0, rd16(0x8E));
    ASSERT_EQ(0xCC, mock_request[0x90]);
    ASSERT_EQ(2, mock_req_size);
}

/*
 * The success tail at 0x00E4D4D2..0x00E4D4F4 copies four fields out of the
 * reply payload into entry_ret; a non-zero status skips it entirely.
 */
TEST(entry_fields_are_copied_only_on_success)
{
    uid_t dir_uid = { 0, 0 };
    uint8_t entry[16];
    status_$t status = 0;
    char name[4] = "a";

    reset_mock();
    GET_ENTRYU_REC.version = 0;
    GET_ENTRYU_REC.base_size = 2;
    memset(entry, 0xEE, sizeof(entry));
    mock_reply[PL(0x00)] = 0x9A;    /* +0x14 word, high byte */
    mock_reply[PL(0x01)] = 0xBC;
    put32(mock_reply, PL(0x02), 0x01020304);    /* reply +0x16 */
    put32(mock_reply, PL(0x06), 0x05060708);    /* reply +0x1A */
    put32(mock_reply, PL(0x0A), 0x090A0B0C);    /* reply +0x1E */

    scrub_stack();
    DIR_$GET_ENTRYU_FUN_00e4d460(&dir_uid, name, 1, entry, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(0x9A, entry[0]);
    ASSERT_EQ(0xBC, entry[1]);
    ASSERT_EQ(0x01020304, entry32(entry, 2));
    ASSERT_EQ(0x05060708, entry32(entry, 6));
    ASSERT_EQ(0x090A0B0C, entry32(entry, 10));

    /* Failure: the caller's buffer is untouched. */
    reset_mock();
    memset(entry, 0xEE, sizeof(entry));
    memcpy(mock_reply + 0x04, &(status_$t){ 0x000E000D }, sizeof(status_$t));
    put32(mock_reply, PL(0x02), 0x01020304);

    scrub_stack();
    DIR_$GET_ENTRYU_FUN_00e4d460(&dir_uid, name, 1, entry, &status);

    ASSERT_EQ(0x000E000D, status);
    ASSERT_EQ(0xEE, entry[0]);
    ASSERT_EQ(0xEE, entry[2]);
    ASSERT_EQ(0xEE, entry[10]);
}

int main(void)
{
    printf("DIR_$GET_ENTRYU_FUN_00e4d460 tests\n");
    RUN_TEST(request_bytes_match_the_stores);
    RUN_TEST(length_and_response_size_arguments);
    RUN_TEST(zero_length_name_copies_nothing);
    RUN_TEST(entry_fields_are_copied_only_on_success);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
