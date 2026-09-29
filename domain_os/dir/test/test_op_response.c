/*
 * dir/test/test_op_response.c - Layout and consumer tests for Dir_$OpResponse
 *
 * The reply record DIR_$DO_OP fills in is a Pascal variant record.  The audit
 * (2026-09-06) found its C model one longword short: f18 was declared as 8
 * bytes, which put every field after it 4 bytes too low.
 *
 * The offsets asserted here come from the assembly:
 *   DIR_$DO_OP (0xE4C02C) addresses the record through A3:
 *     (0x4,A3)  status              0xE4C126, 0xE4C164, 0xE4C18E
 *     (0x8,A3)  reply version       0xE4C174, 0xE4C260
 *     (0xa,A3)  accepted version    0xE4C184, 0xE4C25A
 *     (0x13,A3) remote-reply flag   0xE4C1B6  bset.b #0
 *     (0x14,A3) payload             many
 *     (0x16,A3) UID high            0xE4C1BE move.b, 0xE4C226 pea
 *     (0x1a,A3) UID low             0xE4C1D4, 0xE4C206
 *   DIR_$READ_LINKU (0xE4D6C0) reads length at +0x14 and a UID at +0x16.
 *   DIR_$DROP_LINKU (0xE517F6) reads a UID at +0x14 (lea (-0xc,A6),A0 with the
 *     record based at -0x20).
 *   DIR_$RESOLVE  (0xE4D356) reads +0x14, +0x15, +0x16, +0x1E, +0x26, +0x28,
 *     +0x2A, +0x2C, +0x2E.
 *
 * The _Static_asserts in dir/dir_internal.h only compile under ARCH_M68K, so
 * they are repeated here as run-time checks, and DIR_$RESOLVE is driven
 * through a mocked DIR_$DO_OP to prove it reads the right bytes.
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

#define PAYLOAD_OFF   offsetof(Dir_$OpResponse, uid)

/* ------------------------------------------------------------------ */
/* Mocks for DIR_$RESOLVE                                               */
/* ------------------------------------------------------------------ */

/* The bytes the mocked DIR_$DO_OP stamps into the caller's reply record. */
static uint8_t   mock_reply[sizeof(Dir_$OpResponse)];
static int       mock_do_op_calls;
static int16_t   mock_do_op_resp_size;
static int       mock_loop_replies;

void DIR_$DO_OP(void *request, int16_t req_size, int16_t resp_size,
                void *response, uint16_t *received_len)
{
    (void)request; (void)req_size;
    /* source-32ld: the fifth argument is the reply-length out cell. */
    *received_len = (uint16_t)resp_size;
    mock_do_op_calls++;
    mock_do_op_resp_size = resp_size;
    if (mock_loop_replies > 0 && --mock_loop_replies == 0) {
        mock_reply[PAYLOAD_OFF + 1] = 0x00;     /* stop looping */
    }
    memcpy(response, mock_reply, sizeof(mock_reply));
}

/* 0x00E7FCFE is DIR_$OP_TAB[23].base_size (dir_internal.h), so the table
 * itself is what has to exist here (bead source-wk2f). */
MODULE_DATA_DEFINE(dir_$data_t, DIR_$DATA, 0x00E7DBF8);   /* DIR_$OP_TAB is its op_tab */

#include "../resolve.c"

/* ------------------------------------------------------------------ */
/* Tests                                                                */
/* ------------------------------------------------------------------ */

/*
 * base/base.h declares status_$t as `long`, which is 4 bytes on the m68k
 * target but 8 on a 64-bit host, so the absolute offsets only hold when the
 * two agree (see source-39bo).  The relative layout - which is what the f18[8]
 * -> f18[12] fix is about - is checked unconditionally.
 */
#define LAYOUT_IS_TARGET_EXACT  (sizeof(status_$t) == 4)

TEST(response_layout)
{
    /* Header: f18 follows status and is twelve bytes, not eight. */
    ASSERT_EQ(0x00, offsetof(Dir_$OpResponse, f12));
    ASSERT_EQ(0x01, offsetof(Dir_$OpResponse, f13));
    ASSERT_EQ(0x02, offsetof(Dir_$OpResponse, f14));
    ASSERT_EQ(0x03, offsetof(Dir_$OpResponse, f15));
    ASSERT_EQ(0x04, offsetof(Dir_$OpResponse, status));
    ASSERT_EQ(0x04 + sizeof(status_$t), offsetof(Dir_$OpResponse, f18));
    ASSERT_EQ(12,   sizeof(((Dir_$OpResponse *)0)->f18));
    ASSERT_EQ(11,   DIR_RESP_REMOTE_FLAG_BYTE);
    ASSERT_EQ(offsetof(Dir_$OpResponse, f18) + 12, PAYLOAD_OFF);

    /* Every payload variant starts at the same place. */
    ASSERT_EQ(PAYLOAD_OFF, offsetof(Dir_$OpResponse, cookie));
    ASSERT_EQ(PAYLOAD_OFF, offsetof(Dir_$OpResponse, _20_2_));
    ASSERT_EQ(PAYLOAD_OFF, offsetof(Dir_$OpResponse, raw));
    ASSERT_EQ(PAYLOAD_OFF, offsetof(Dir_$OpResponse, resolve.more));

    /* Offsets within the payload, as the assembly addresses them. */
    ASSERT_EQ(PAYLOAD_OFF + 0x04, offsetof(Dir_$OpResponse, uid.low));
    ASSERT_EQ(PAYLOAD_OFF + 0x02, offsetof(Dir_$OpResponse, _22_4_));
    ASSERT_EQ(PAYLOAD_OFF + 0x06, offsetof(Dir_$OpResponse, f1a));
    ASSERT_EQ(PAYLOAD_OFF + 0x0A, offsetof(Dir_$OpResponse, _24_4_));
    ASSERT_EQ(PAYLOAD_OFF + 0x01, offsetof(Dir_$OpResponse, resolve.loop));
    ASSERT_EQ(PAYLOAD_OFF + 0x02, offsetof(Dir_$OpResponse, resolve.start_uid));
    ASSERT_EQ(PAYLOAD_OFF + 0x0A, offsetof(Dir_$OpResponse, resolve.resolved_uid));
    ASSERT_EQ(PAYLOAD_OFF + 0x12, offsetof(Dir_$OpResponse, resolve.param5));
    ASSERT_EQ(PAYLOAD_OFF + 0x14, offsetof(Dir_$OpResponse, resolve.param6));
    ASSERT_EQ(PAYLOAD_OFF + 0x16, offsetof(Dir_$OpResponse, resolve.param7));
    ASSERT_EQ(PAYLOAD_OFF + 0x18, offsetof(Dir_$OpResponse, resolve.param8));
    ASSERT_EQ(PAYLOAD_OFF + 0x1A, offsetof(Dir_$OpResponse, resolve.link_count));
    /* DIR_$DO_OP addresses as far as (0x40,A3), i.e. 0x2C past the payload,
     * and DIR_$GET_DEF_PROTECTION (0x00E51E00) reads a full UID there while
     * asking DIR_$DO_OP for a 0x48-byte reply, so the record runs to 0x47. */
    ASSERT_EQ(PAYLOAD_OFF + 0x34, sizeof(Dir_$OpResponse));

    if (LAYOUT_IS_TARGET_EXACT) {
        ASSERT_EQ(0x08, offsetof(Dir_$OpResponse, f18));
        ASSERT_EQ(0x13, offsetof(Dir_$OpResponse, f18) + DIR_RESP_REMOTE_FLAG_BYTE);
        ASSERT_EQ(0x14, PAYLOAD_OFF);
        ASSERT_EQ(0x16, offsetof(Dir_$OpResponse, _22_4_));
        ASSERT_EQ(0x1A, offsetof(Dir_$OpResponse, f1a));
        ASSERT_EQ(0x1E, offsetof(Dir_$OpResponse, _24_4_));
        ASSERT_EQ(0x2E, offsetof(Dir_$OpResponse, resolve.link_count));
        ASSERT_EQ(0x14, offsetof(Dir_$OpResponse, entry.word));
        ASSERT_EQ(0x16, offsetof(Dir_$OpResponse, entry.uid));
        ASSERT_EQ(0x1E, offsetof(Dir_$OpResponse, entry.extra));
        /* DIR_$GET_DEF_PROTECTION asks for a 0x48-byte reply and reads a
         * full uid out of reply+0x40 (0x00E51D9E / 0x00E51E00). */
        ASSERT_EQ(0x48, sizeof(Dir_$OpResponse));
    }
}

/*
 * Setting a field through the struct must land on the byte offset the
 * assembly uses.  With the old f18[8] every one of these is four bytes low.
 */
TEST(fields_land_on_the_assembly_offsets)
{
    Dir_$OpResponse resp;
    uint8_t *b = (uint8_t *)&resp;
    size_t pl = PAYLOAD_OFF;

    memset(&resp, 0, sizeof(resp));
    resp.status = 0x0A0B0C0D;
    ASSERT_EQ(0x0A0B0C0D, *(uint32_t *)(b + 0x04));

    memset(&resp, 0, sizeof(resp));
    resp.f18[DIR_RESP_REMOTE_FLAG_BYTE] |= DIR_RESP_REMOTE_FLAG;
    ASSERT_EQ(0x01, b[pl - 1]);         /* the byte just below the payload */

    memset(&resp, 0, sizeof(resp));
    resp.uid.high = 0x11111111;
    resp.uid.low  = 0x22222222;
    ASSERT_EQ(0x11111111, *(uint32_t *)(b + pl + 0x00));
    ASSERT_EQ(0x22222222, *(uint32_t *)(b + pl + 0x04));

    memset(&resp, 0, sizeof(resp));
    resp._20_2_ = 0x3333;
    resp._22_4_ = 0x44444444;
    resp.f1a    = 0x55555555;
    resp._24_4_ = 0x66666666;
    ASSERT_EQ(0x3333,     *(uint16_t *)(b + pl + 0x00));
    ASSERT_EQ(0x44444444, *(uint32_t *)(b + pl + 0x02));
    ASSERT_EQ(0x55555555, *(uint32_t *)(b + pl + 0x06));
    ASSERT_EQ(0x66666666, *(uint32_t *)(b + pl + 0x0A));

    /* cookie and uid.high are the same longword (DIR_READU vs. delete). */
    memset(&resp, 0, sizeof(resp));
    resp.cookie = 0x77777777;
    ASSERT_EQ(0x77777777, resp.uid.high);
}

/* ------------------------------------------------------------------ */
/* DIR_$RESOLVE reads the reply at the offsets the epilogue uses         */
/* ------------------------------------------------------------------ */

/* Payload offsets are given relative to the payload base so the tests hold
 * whatever size the host gives status_$t (see source-39bo). */
#define PL(off)  (PAYLOAD_OFF + (off))
static void put32(size_t off, uint32_t v) { memcpy(mock_reply + off, &v, 4); }
static void put16(size_t off, uint16_t v) { memcpy(mock_reply + off, &v, 2); }

TEST(resolve_reads_reply_at_the_right_offsets)
{
    uid_t start = { 0, 0 }, resolved = { 0, 0 };
    uint16_t p5 = 0, p6 = 0, p7 = 0, p8 = 0, links = 0;
    uint16_t path_len = 4;
    status_$t status = 0xdeadbeef;
    char path[8] = "abcd";

    memset(mock_reply, 0, sizeof(mock_reply));
    mock_do_op_calls = 0;

    memcpy(mock_reply + 0x04, &(status_$t){ status_$ok }, sizeof(status_$t));
    mock_reply[PL(0x00)] = 0xFF;    /* more: negative => outputs are valid */
    mock_reply[PL(0x01)] = 0x00;    /* loop: non-negative => stop */
    put32(PL(0x02), 0xAAAA0001);    /* start_uid.high    (record +0x16) */
    put32(PL(0x06), 0xAAAA0002);    /* start_uid.low     (record +0x1A) */
    put32(PL(0x0A), 0xBBBB0001);    /* resolved_uid.high (record +0x1E) */
    put32(PL(0x0E), 0xBBBB0002);    /* resolved_uid.low  (record +0x22) */
    put16(PL(0x12), 0x0505);        /* param5            (record +0x26) */
    put16(PL(0x14), 0x0606);        /* param6            (record +0x28) */
    put16(PL(0x16), 0x0707);        /* param7            (record +0x2A) */
    put16(PL(0x18), 0x0808);        /* param8            (record +0x2C) */
    put16(PL(0x1A), 0x0909);        /* link_count        (record +0x2E) */

    DIR_$RESOLVE(path, &path_len, &start, &resolved,
                 &p5, &p6, &p7, &p8, 0, &links, &status);

    ASSERT_EQ(1, mock_do_op_calls);
    ASSERT_EQ(0x34, mock_do_op_resp_size);
    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(0xAAAA0001, start.high);
    ASSERT_EQ(0xAAAA0002, start.low);
    ASSERT_EQ(0xBBBB0001, resolved.high);
    ASSERT_EQ(0xBBBB0002, resolved.low);
    ASSERT_EQ(0x0505, p5);
    ASSERT_EQ(0x0606, p6);
    ASSERT_EQ(0x0707, p7);
    ASSERT_EQ(0x0808, p8);
    ASSERT_EQ(0x0909, links);
}

/*
 * A non-negative byte at 0x14 means the resolution finished at the server and
 * the outputs must be left alone (0xE4D418 tst.b (-0x2c,A6) / bpl).
 */
TEST(resolve_stops_on_non_negative_more_byte)
{
    uid_t start = { 1, 2 }, resolved = { 3, 4 };
    uint16_t p5 = 9, p6 = 9, p7 = 9, p8 = 9, links = 9;
    uint16_t path_len = 4;
    status_$t status = 0xdeadbeef;
    char path[8] = "abcd";

    memset(mock_reply, 0, sizeof(mock_reply));
    mock_do_op_calls = 0;
    memcpy(mock_reply + 0x04, &(status_$t){ status_$ok }, sizeof(status_$t));
    mock_reply[PL(0x00)] = 0x00;    /* more: not negative */
    put32(PL(0x02), 0xDEAD0001);

    DIR_$RESOLVE(path, &path_len, &start, &resolved,
                 &p5, &p6, &p7, &p8, 0, &links, &status);

    ASSERT_EQ(1, mock_do_op_calls);
    ASSERT_EQ(1, start.high);       /* untouched */
    ASSERT_EQ(3, resolved.high);
    ASSERT_EQ(0, links);            /* cleared on entry, never refilled */
}

/*
 * The byte at record offset 0x15 is the loop flag: DIR_$RESOLVE issues another
 * request while it is negative (0xE4D44E tst.b (-0x2b,A6) / bmi).  The mock
 * clears it on the third reply so the loop terminates.
 */
TEST(resolve_loops_on_negative_loop_byte)
{
    uid_t start = { 0, 0 }, resolved = { 0, 0 };
    uint16_t p5 = 0, p6 = 0, p7 = 0, p8 = 0, links = 0;
    uint16_t path_len = 4;
    status_$t status = 0xdeadbeef;
    char path[8] = "abcd";

    memset(mock_reply, 0, sizeof(mock_reply));
    mock_do_op_calls = 0;
    mock_loop_replies = 3;
    memcpy(mock_reply + 0x04, &(status_$t){ status_$ok }, sizeof(status_$t));
    mock_reply[PL(0x00)] = 0xFF;    /* more */
    mock_reply[PL(0x01)] = 0xFF;    /* loop: keep going */

    DIR_$RESOLVE(path, &path_len, &start, &resolved,
                 &p5, &p6, &p7, &p8, 0, &links, &status);

    ASSERT_EQ(3, mock_do_op_calls);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("Dir_$OpResponse tests\n");
    RUN_TEST(response_layout);
    RUN_TEST(fields_land_on_the_assembly_offsets);
    RUN_TEST(resolve_reads_reply_at_the_right_offsets);
    RUN_TEST(resolve_stops_on_non_negative_more_byte);
    RUN_TEST(resolve_loops_on_negative_loop_byte);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
