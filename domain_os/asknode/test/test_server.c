/*
 * asknode/test/test_server.c - unit tests for ASKNODE_$SERVER (0x00E6597A)
 *
 * These tests #include asknode/server.c and drive the real ASKNODE_$SERVER
 * through mocked callees, so what is exercised is the emitted translation and
 * not a re-implementation.
 *
 * The behaviours pinned down here are the ones bead source-ai1l called out:
 *
 *   - the 92-entry jump table at 0x00E65A60: which request types have a body
 *     of their own, which delegate to ASKNODE_$INTERNET_INFO, and which fall
 *     into the "unknown request type" arm;
 *   - everything the received reply header is read for happens BEFORE
 *     NETBUF_$RTN_HDR gives the buffer back (0x00E65A12), and PKT_$DUMP_DATA
 *     gets the APP_$RECEIVE record's own page vector (0x00E659BA);
 *   - the PKT_$SEND_INTERNET argument shape at 0x00E65E26, including the two
 *     distinct word locals for arguments 13 and 14 and the fact that argument
 *     11 is whatever A3 holds.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

/* ==========================================================================
 * Test infrastructure
 * ========================================================================== */

static int tests_passed = 0;
static int tests_failed = 0;
static int current_failed = 0;
static void reset_mocks(void);

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do {                                                   \
    printf("  Running %s... ", #name);                                        \
    current_failed = 0;                                                       \
    reset_mocks();                                                            \
    test_##name();                                                            \
    if (current_failed == 0) { tests_passed++; printf("PASSED\n"); }          \
} while (0)

#define ASSERT_EQ(expected, actual) do {                                      \
    if ((unsigned long long)(expected) != (unsigned long long)(actual)) {     \
        printf("FAILED\n    Expected: 0x%llx, Got: 0x%llx at line %d\n",      \
               (unsigned long long)(expected),                               \
               (unsigned long long)(actual), __LINE__);                      \
        tests_failed++; current_failed = 1;                                   \
        return;                                                               \
    }                                                                         \
} while (0)

#define ASSERT_TRUE(cond) do {                                                \
    if (!(cond)) {                                                            \
        printf("FAILED\n    Assertion failed at line %d: %s\n",               \
               __LINE__, #cond);                                              \
        tests_failed++; current_failed = 1;                                   \
        return;                                                               \
    }                                                                         \
} while (0)

/* ==========================================================================
 * Headers the translation unit under test needs
 * ========================================================================== */

#include "asknode/asknode_internal.h"

/* ==========================================================================
 * Kernel data
 * ========================================================================== */

uint32_t NODE_$ME;
uint32_t TIME_$CURRENT_CLOCKH;
network_$failure_rec_t NETWORK_$FAILURE_REC;
uint32_t ASKNODE_$EMPTY_DATA;
uint32_t PKT_$DEFAULT_INFO[8];

/* ==========================================================================
 * Mock state
 * ========================================================================== */

#define MOCK_REPLY_VA       ((void *)&mock_reply_hdr)
#define MOCK_DATA_VA        ((void *)mock_payload)
#define MOCK_LOG_VA         0x00051000u

static asknode_$reply_hdr_t mock_reply_hdr;
static uint8_t   mock_payload[0x40];

static int       mock_receive_fails;
static uint8_t   mock_rcv_flags27;      /* record+0x27, bit 7 = "do not answer" */

static int       mock_dump_calls;
static void     *mock_dump_pages;
static uint16_t  mock_dump_len;
static int       mock_rtn_hdr_calls;
static int       mock_dump_before_rtn;  /* PKT_$DUMP_DATA seen before RTN_HDR */
static uint32_t  mock_rtn_hdr_arg;

static int       mock_copy_calls;
static int32_t   mock_copy_len;
static const void *mock_copy_src;

static int16_t   mock_validate_result;
static int       mock_validate_calls;
static int32_t   mock_validate_key;
static int8_t    mock_validate_is_local;

static int       mock_internet_info_calls;
static uint16_t  mock_internet_info_type;

static int       mock_send_calls;
static uint32_t  mock_send_routing_key;
static uint32_t  mock_send_dest_node;
static uint16_t  mock_send_dest_sock;
static int32_t   mock_send_src_node_or;
static uint32_t  mock_send_src_node;
static uint16_t  mock_send_src_sock;
static uint16_t  mock_send_request_id;
static uint16_t  mock_send_template_len;
static void     *mock_send_data;
static int16_t   mock_send_data_len;
static uint16_t *mock_send_retry_hint;
static uint16_t *mock_send_timeout_out;
static asknode_response_t mock_send_reply;

static int       mock_log_read_calls;
static int       mock_log_read2_calls;
static uint16_t  mock_log_read_len;
static uint16_t  mock_log_read2_offset;
static uint16_t  mock_log_count;

static int       mock_get_dat_calls;
static int       mock_rtn_dat_calls;
static uint32_t  mock_getva_status;

static int       mock_time_clock_calls;

static void reset_mocks(void)
{
    memset(&mock_reply_hdr, 0, sizeof(mock_reply_hdr));
    memset(mock_payload, 0, sizeof(mock_payload));
    memset(&NETWORK_$FAILURE_REC, 0, sizeof(NETWORK_$FAILURE_REC));
    memset(PKT_$DEFAULT_INFO, 0, sizeof(PKT_$DEFAULT_INFO));

    NODE_$ME = 0x00012345;
    TIME_$CURRENT_CLOCKH = 0x11223344;
    ASKNODE_$EMPTY_DATA = 0;

    mock_receive_fails = 0;
    mock_rcv_flags27 = 0;
    mock_dump_calls = 0; mock_dump_pages = NULL; mock_dump_len = 0;
    mock_rtn_hdr_calls = 0; mock_dump_before_rtn = 0; mock_rtn_hdr_arg = 0;
    mock_copy_calls = 0; mock_copy_len = 0; mock_copy_src = NULL;
    mock_validate_result = 1; mock_validate_calls = 0;
    mock_validate_key = 0; mock_validate_is_local = 0;
    mock_internet_info_calls = 0; mock_internet_info_type = 0xFFFF;
    mock_send_calls = 0;
    mock_send_routing_key = 0; mock_send_dest_node = 0; mock_send_dest_sock = 0;
    mock_send_src_node_or = 0; mock_send_src_node = 0; mock_send_src_sock = 0;
    mock_send_request_id = 0; mock_send_template_len = 0;
    mock_send_data = NULL; mock_send_data_len = 0;
    mock_send_retry_hint = NULL; mock_send_timeout_out = NULL;
    memset(&mock_send_reply, 0, sizeof(mock_send_reply));
    mock_log_read_calls = 0; mock_log_read2_calls = 0;
    mock_log_read_len = 0; mock_log_read2_offset = 0; mock_log_count = 0x30;
    mock_get_dat_calls = 0; mock_rtn_dat_calls = 0; mock_getva_status = 0;
    mock_time_clock_calls = 0;
}

/* ==========================================================================
 * Mocked callees
 * ========================================================================== */

void APP_$RECEIVE(uint16_t sock_num, void *result, status_$t *status_ret)
{
    app_$receive_rec_t *r = (app_$receive_rec_t *)result;

    if (sock_num != 4) {
        printf("FAILED\n    APP_$RECEIVE socket %u at line %d\n",
               sock_num, __LINE__);
        tests_failed++; current_failed = 1;
    }
    if (mock_receive_fails) {
        *status_ret = 0x00110006;
        return;
    }
    memset(r, 0, sizeof(*r));
    r->reply = ARCH_PTR_TO_VA(MOCK_REPLY_VA);
    r->data  = ARCH_PTR_TO_VA(MOCK_DATA_VA);
    r->data_pages[0] = 0x1000;
    r->data_pages[1] = 0x2000;
    r->hdr_f06 = 0;                 /* src_node_or */
    r->hdr_f12 = 0x0000ABCD;        /* routing key */
    r->flags_hi = (uint16_t)mock_rcv_flags27;   /* low byte is record+0x27 */
    *status_ret = 0;
}

void PKT_$DUMP_DATA(uint32_t *pages, int16_t len)
{
    mock_dump_calls++;
    mock_dump_pages = pages;
    mock_dump_len = (uint16_t)len;
    if (mock_rtn_hdr_calls == 0) {
        mock_dump_before_rtn = 1;
    }
}

void OS_$DATA_COPY(const void *src, void *dst, uint32_t len)
{
    mock_copy_calls++;
    mock_copy_src = src;
    mock_copy_len = (int32_t)len;
    if (len > 0) {
        memcpy(dst, src, (size_t)len);
    }
}

void NETBUF_$RTN_HDR(uint32_t *va_ptr)
{
    mock_rtn_hdr_calls++;
    mock_rtn_hdr_arg = (uint32_t)(uintptr_t)va_ptr;
}

int16_t ROUTE_$VALIDATE_PORT(int32_t routing_key, int8_t is_local)
{
    mock_validate_calls++;
    mock_validate_key = routing_key;
    mock_validate_is_local = is_local;
    return mock_validate_result;
}

void TIME_$CLOCK(clock_t *out)
{
    mock_time_clock_calls++;
    ((uint16_t *)out)[0] = 0xDEAD;
    ((uint32_t *)((uint8_t *)out + 2))[0] = 0x80000010u;
}

long M$OIS$LLL(long a, long b)
{
    return a - b;
}

void NETBUF_$GET_DAT(uint32_t *addr_out)
{
    mock_get_dat_calls++;
    *addr_out = 0x00000400u;
}

void NETBUF_$GETVA(uint32_t ppn_shifted, uint32_t *va_out, status_$t *status)
{
    (void)ppn_shifted;
    *va_out = MOCK_LOG_VA;
    *status = (status_$t)mock_getva_status;
}

uint32_t NETBUF_$RTNVA(uint32_t *va_ptr)
{
    return *va_ptr;
}

void NETBUF_$RTN_DAT(uint32_t addr)
{
    (void)addr;
    mock_rtn_dat_calls++;
}

void LOG_$READ(void *buffer, uint16_t *max_len, uint16_t *actual_len)
{
    (void)buffer;
    mock_log_read_calls++;
    mock_log_read_len = *max_len;
    *actual_len = mock_log_count;
}

void LOG_$READ2(void *buffer, uint16_t offset, uint16_t max_len,
                uint16_t *actual_len)
{
    (void)buffer; (void)max_len;
    mock_log_read2_calls++;
    mock_log_read2_offset = offset;
    *actual_len = mock_log_count;
}

uint32_t ASKNODE_$INTERNET_INFO(uint16_t *req_type, uint32_t *node_id,
                                int32_t *req_len, uid_t *param,
                                uint16_t *resp_len, uint32_t *result,
                                status_$t *status)
{
    (void)node_id; (void)req_len; (void)param; (void)result;
    mock_internet_info_calls++;
    mock_internet_info_type = *req_type;
    if (*resp_len != 0x200) {
        printf("FAILED\n    resp_len 0x%x at line %d\n", *resp_len, __LINE__);
        tests_failed++; current_failed = 1;
    }
    *status = 0;
    return 0;
}

void PKT_$SEND_INTERNET(uint32_t routing_key, uint32_t dest_node,
                        uint16_t dest_sock, int32_t src_node_or,
                        uint32_t src_node, uint16_t src_sock,
                        void *pkt_info, uint16_t request_id,
                        void *template, uint16_t template_len,
                        void *data, int16_t data_len,
                        uint16_t *retry_hint, uint16_t *timeout_out,
                        status_$t *status_ret)
{
    (void)pkt_info;
    mock_send_calls++;
    memcpy(&mock_send_reply, template, sizeof(mock_send_reply));
    mock_send_routing_key  = routing_key;
    mock_send_dest_node    = dest_node;
    mock_send_dest_sock    = dest_sock;
    mock_send_src_node_or  = src_node_or;
    mock_send_src_node     = src_node;
    mock_send_src_sock     = src_sock;
    mock_send_request_id   = request_id;
    mock_send_template_len = template_len;
    mock_send_data         = data;
    mock_send_data_len     = data_len;
    mock_send_retry_hint   = retry_hint;
    mock_send_timeout_out  = timeout_out;
    *retry_hint = 5;
    *timeout_out = 4;
    *status_ret = 0;
}

/* ==========================================================================
 * The translation unit under test
 * ========================================================================== */

#include "../server.c"

/* ==========================================================================
 * Helpers
 * ========================================================================== */

/* The reply record ASKNODE_$SERVER handed to PKT_$SEND_INTERNET */
static uint16_t mock_reply_version_of_last_send(void) { return mock_send_reply.version; }
static uint16_t mock_reply_type_of_last_send(void)    { return mock_send_reply.response_type; }
static uint32_t mock_reply_status_of_last_send(void)  { return (uint32_t)mock_send_reply.status; }
static uint32_t mock_reply_node_of_last_send(void)    { return mock_send_reply.node_id; }
static uint16_t mock_reply_flags_of_last_send(void)   { return mock_send_reply.flags; }
static int16_t  mock_reply_count_of_last_send(void)   { return mock_send_reply.count; }

static asknode_$server_ctx_t ctx;
static int32_t routing_info;

/*
 * Build a received request in the mock reply header plus payload and run the
 * server.  `req` is copied into the payload the way the wire packet carries
 * it; `len` is the header's length word.
 */
static void run_request(const asknode_request_t *req, uint16_t len)
{
    memset(&ctx, 0, sizeof(ctx));
    routing_info = 0;

    mock_reply_hdr.magic       = 0x0118;
    mock_reply_hdr.length      = len;
    mock_reply_hdr.data_len    = 0x40;
    mock_reply_hdr.reply_id    = 0x1234;
    mock_reply_hdr.sender_node = 0x00099999;
    mock_reply_hdr.node_id     = 0x000AAAAA;
    mock_reply_hdr.src_socket  = 0x0009;
    mock_reply_hdr.f14         = 0;

    memcpy(mock_payload, req, sizeof(*req));

    ASKNODE_$SERVER(&ctx, &routing_info);
}

/* ==========================================================================
 * Tests
 * ========================================================================== */

/*
 * The record layouts the whole function depends on.
 */
TEST(record_layouts)
{
    /*
     * app_$receive_rec_t.reply and .data are target virtual addresses, so
     * the record lays out identically on the host and its _Static_asserts in
     * app/app.h now run unguarded.  The field order the function depends on
     * is checked here as well.
     */
    ASSERT_TRUE(offsetof(app_$receive_rec_t, reply) <
                offsetof(app_$receive_rec_t, data));
    ASSERT_TRUE(offsetof(app_$receive_rec_t, data) <
                offsetof(app_$receive_rec_t, data_pages));

    ASSERT_EQ(0x18, sizeof(asknode_request_t));
    ASSERT_EQ(0x10, offsetof(asknode_request_t, forwarded));
    ASSERT_EQ(0x12, offsetof(asknode_request_t, count));
    ASSERT_EQ(0x14, offsetof(asknode_request_t, param3));

    ASSERT_EQ(0x22, sizeof(asknode_$server_ctx_t));
    ASSERT_EQ(0x18, offsetof(asknode_$server_ctx_t, request_id));
    ASSERT_EQ(0x1A, offsetof(asknode_$server_ctx_t, socket));

    ASSERT_EQ(0x04, offsetof(asknode_$reply_hdr_t, data_len));
    ASSERT_EQ(0x06, offsetof(asknode_$reply_hdr_t, reply_id));
    ASSERT_EQ(0x0E, offsetof(asknode_$reply_hdr_t, node_id));
    ASSERT_EQ(0x12, offsetof(asknode_$reply_hdr_t, src_socket));
}

/*
 * The jump table at 0x00E65A60, transcribed from the image.  Each entry is a
 * word offset from the table base; 0x00B8/0x0148/0x021E/0x02A4/0x02C4 are the
 * five bodies, 0x027A is the ASKNODE_$INTERNET_INFO arm and 0x0358 is the
 * "unknown request type" arm.
 */
static const uint16_t asknode_$jump_table[0x5C] = {
    0x00B8, 0x0358, 0x027A, 0x0358, 0x027A, 0x0358, 0x027A, 0x0358,
    0x027A, 0x0358, 0x027A, 0x0358, 0x027A, 0x0358, 0x02A4, 0x0358,
    0x027A, 0x0358, 0x027A, 0x0358, 0x027A, 0x0358, 0x027A, 0x0358,
    0x027A, 0x0358, 0x027A, 0x0358, 0x027A, 0x0358, 0x0358, 0x0358,
    0x0358, 0x027A, 0x0358, 0x027A, 0x0358, 0x027A, 0x0358, 0x027A,
    0x0358, 0x027A, 0x0358, 0x027A, 0x0358, 0x0148, 0x0358, 0x027A,
    0x0358, 0x02C4, 0x0358, 0x027A, 0x0358, 0x027A, 0x0358, 0x027A,
    0x0358, 0x027A, 0x0358, 0x027A, 0x0358, 0x027A, 0x0358, 0x027A,
    0x0358, 0x027A, 0x0358, 0x027A, 0x0358, 0x021E, 0x0358, 0x027A,
    0x0358, 0x027A, 0x0358, 0x027A, 0x0358, 0x027A, 0x0358, 0x027A,
    0x0358, 0x027A, 0x0358, 0x0358, 0x0358, 0x027A, 0x0358, 0x027A,
    0x0358, 0x027A, 0x0358, 0x027A
};

#define JT_WHO          0x00B8
#define JT_WHO_REMOTE   0x0148
#define JT_TIME_SYNC    0x021E
#define JT_DELEGATE     0x027A
#define JT_FAILURE_REC  0x02A4
#define JT_LOG_READ     0x02C4
#define JT_UNKNOWN      0x0358

/*
 * asknode_$server_delegates must agree with the table for every index, and
 * the five request types with bodies of their own must not be in it.
 */
TEST(jump_table_delegate_set)
{
    int i;

    for (i = 0; i < 0x5C; i++) {
        int expect = (asknode_$jump_table[i] == JT_DELEGATE);
        int got = (asknode_$server_delegates((uint16_t)i) < 0);
        if (expect != got) {
            printf("FAILED\n    request type 0x%02x: table says %s, "
                   "asknode_$server_delegates says %s\n",
                   i, expect ? "delegate" : "not delegate",
                   got ? "delegate" : "not delegate");
            tests_failed++; current_failed = 1;
            return;
        }
    }

    /* The five own-body types are never delegates */
    ASSERT_EQ(JT_WHO,         asknode_$jump_table[ASKNODE_REQ_WHO]);
    ASSERT_EQ(JT_FAILURE_REC, asknode_$jump_table[ASKNODE_REQ_RECORD_FAILURE]);
    ASSERT_EQ(JT_WHO_REMOTE,  asknode_$jump_table[ASKNODE_REQ_WHO_REMOTE]);
    ASSERT_EQ(JT_LOG_READ,    asknode_$jump_table[ASKNODE_REQ_LOG_READ]);
    ASSERT_EQ(JT_TIME_SYNC,   asknode_$jump_table[ASKNODE_REQ_TIME_SYNC]);
}

/* Every index the table sends to the delegate arm really delegates. */
TEST(delegated_request_calls_internet_info)
{
    asknode_request_t req;
    int i;

    for (i = 0; i < 0x5C; i++) {
        if (asknode_$jump_table[i] != JT_DELEGATE) {
            continue;
        }
        reset_mocks();
        memset(&req, 0, sizeof(req));
        req.version = 3;
        req.request_type = (uint16_t)i;
        run_request(&req, 0x18);

        ASSERT_EQ(1, mock_internet_info_calls);
        ASSERT_EQ(i, mock_internet_info_type);
        ASSERT_EQ(1, mock_send_calls);
        /* the reply type is always request + 1 (0x00E65A1E) */
        ASSERT_EQ(i + 1, mock_reply_type_of_last_send());
    }
}

/*
 * Anything the table sends to 0x00E65DB8, and anything at or above 0x5C,
 * answers "unknown request type" with a zero reply type.
 */
TEST(unknown_request_type)
{
    asknode_request_t req;
    int i;

    for (i = 0; i < 0x5C; i++) {
        if (asknode_$jump_table[i] != JT_UNKNOWN) {
            continue;
        }
        reset_mocks();
        memset(&req, 0, sizeof(req));
        req.version = 3;
        req.request_type = (uint16_t)i;
        run_request(&req, 0x18);

        ASSERT_EQ(0, mock_internet_info_calls);
        ASSERT_EQ(1, mock_send_calls);
        ASSERT_EQ(0, mock_reply_type_of_last_send());
        ASSERT_EQ(0x11000D, mock_reply_status_of_last_send());
    }

    /* 0x5C itself, and a word that is negative as an int16 */
    reset_mocks();
    memset(&req, 0, sizeof(req));
    req.version = 3;
    req.request_type = 0x5C;
    run_request(&req, 0x18);
    ASSERT_EQ(0, mock_internet_info_calls);
    ASSERT_EQ(0x11000D, mock_reply_status_of_last_send());

    reset_mocks();
    memset(&req, 0, sizeof(req));
    req.version = 3;
    req.request_type = 0xFFFF;
    run_request(&req, 0x18);
    ASSERT_EQ(0, mock_internet_info_calls);
    ASSERT_EQ(0x11000D, mock_reply_status_of_last_send());
}

/*
 * PKT_$DUMP_DATA takes the receive record's own page vector and runs before
 * the header buffer goes back (0x00E659B0 vs 0x00E65A12).
 */
TEST(dump_data_precedes_return_of_header)
{
    asknode_request_t req;

    memset(&req, 0, sizeof(req));
    req.version = 3;
    req.request_type = 0x02;        /* a delegate */
    run_request(&req, 0x18);

    ASSERT_EQ(1, mock_dump_calls);
    ASSERT_EQ(1, mock_dump_before_rtn);
    ASSERT_EQ(0x40, mock_dump_len);
    ASSERT_EQ(1, mock_rtn_hdr_calls);
    /* the copy source is the record's data pointer, not the reply pointer */
    ASSERT_TRUE(mock_copy_src == MOCK_DATA_VA);
    ASSERT_EQ(0x18, mock_copy_len);
}

/* A short packet clamps the request copy to the header's length word. */
TEST(request_copy_is_clamped)
{
    asknode_request_t req;

    memset(&req, 0, sizeof(req));
    req.version = 3;
    req.request_type = 0x02;
    run_request(&req, 0x0A);
    ASSERT_EQ(0x0A, mock_copy_len);

    reset_mocks();
    run_request(&req, 0x40);
    ASSERT_EQ(0x18, mock_copy_len);
}

/* The reply version follows the request's (0x00E65A28). */
TEST(reply_version_follows_request)
{
    asknode_request_t req;

    memset(&req, 0, sizeof(req));
    req.version = 2;
    req.request_type = 0x02;
    run_request(&req, 0x18);
    ASSERT_EQ(2, mock_reply_version_of_last_send());

    reset_mocks();
    req.version = 7;
    run_request(&req, 0x18);
    ASSERT_EQ(3, mock_reply_version_of_last_send());
}

/*
 * The WHO body: hop counter in the HIGH word of request+0x08, the reply's
 * node id and flags, ROUTE_$VALIDATE_PORT with a true "is local", and the
 * propagation decision.
 */
TEST(who_request_body)
{
    asknode_request_t req;

    memset(&req, 0, sizeof(req));
    req.version = 3;
    req.request_type = ASKNODE_REQ_WHO;
    req.node_id = 0x00077777;               /* != NODE_$ME */
    req.param1  = 0x00030000u;              /* hop counter 3 in the high word */
    run_request(&req, 0x18);

    ASSERT_EQ(1, mock_validate_calls);
    ASSERT_EQ((int8_t)0xFF, mock_validate_is_local);
    /* src_node_or is 0, so the routing key comes from the record's +0x1C */
    ASSERT_EQ(0x0000ABCD, (uint32_t)routing_info);
    ASSERT_EQ(0x0000ABCD, mock_validate_key);

    ASSERT_EQ(1, mock_send_calls);
    ASSERT_EQ(NODE_$ME, mock_reply_node_of_last_send());
    ASSERT_EQ(0xB1FF, mock_reply_flags_of_last_send());
    ASSERT_EQ(3, mock_reply_count_of_last_send());
    ASSERT_EQ(0x1000, ctx.clock_lo);
    ASSERT_EQ(0, ctx.request_type);

    /* propagation: status ok, counter still > 0, node != me, bit 2 clear */
    ASSERT_EQ(3, ctx.version);
    ASSERT_EQ(0x00077777, ctx.node_id);
    ASSERT_EQ(0x1234, (uint16_t)ctx.request_id);
    ASSERT_EQ(0x0009, ctx.socket);
    /* the counter the context carries on is one lower */
    ASSERT_EQ(2, (int16_t)(ctx.param1 >> 16));
}

/* Bit 2 of the received header's +0x14 suppresses propagation. */
TEST(who_request_flag_bit2_stops_propagation)
{
    asknode_request_t req;

    memset(&req, 0, sizeof(req));
    req.version = 3;
    req.request_type = ASKNODE_REQ_WHO;
    req.node_id = 0x00077777;
    req.param1  = 0x00030000u;
    memset(&ctx, 0, sizeof(ctx));
    routing_info = 0;

    mock_reply_hdr.magic       = 0x0118;
    mock_reply_hdr.length      = 0x18;
    mock_reply_hdr.data_len    = 0x40;
    mock_reply_hdr.reply_id    = 0x1234;
    mock_reply_hdr.sender_node = 0x00099999;
    mock_reply_hdr.node_id     = 0x000AAAAA;
    mock_reply_hdr.src_socket  = 0x0009;
    mock_reply_hdr.f14         = 0x04;      /* bit 2 set */
    memcpy(mock_payload, &req, sizeof(req));

    ASKNODE_$SERVER(&ctx, &routing_info);

    ASSERT_EQ(1, mock_send_calls);
    ASSERT_EQ(0, ctx.version);              /* context never filled in */
}

/*
 * Bit 7 of the receive record's +0x27 makes the server answer nothing at all
 * (0x00E65B18 / 0x00E65BA8).
 */
TEST(who_request_suppressed_by_record_flag)
{
    asknode_request_t req;

    memset(&req, 0, sizeof(req));
    req.version = 3;
    req.request_type = ASKNODE_REQ_WHO;
    mock_rcv_flags27 = 0x80;
    run_request(&req, 0x18);

    ASSERT_EQ(1, mock_dump_calls);          /* the pages still go back */
    ASSERT_EQ(1, mock_rtn_hdr_calls);
    ASSERT_EQ(0, mock_send_calls);
    ASSERT_EQ(0, mock_validate_calls);

    reset_mocks();
    mock_rcv_flags27 = 0x80;
    req.request_type = ASKNODE_REQ_WHO_REMOTE;
    run_request(&req, 0x18);
    ASSERT_EQ(0, mock_send_calls);
}

/* WHO_REMOTE answers 1 when it is the target and 0x2E otherwise. */
TEST(who_remote_target_selection)
{
    asknode_request_t req;

    memset(&req, 0, sizeof(req));
    req.version = 3;
    req.request_type = ASKNODE_REQ_WHO_REMOTE;
    req.node_id   = NODE_$ME;
    req.param1    = 0x00055555;
    req.forwarded = 0;
    req.count     = 2;
    req.param3    = 0x0BADF00D;
    run_request(&req, 0x18);

    ASSERT_EQ(1, mock_reply_type_of_last_send());
    ASSERT_EQ(0x00055555, mock_reply_node_of_last_send());
    ASSERT_EQ(0x00055555, mock_send_dest_node);
    ASSERT_EQ(0x0BADF00D, ctx.clock_lo);
    ASSERT_EQ(0x2D, ctx.request_type);

    /* forwarded true takes the other arm even when the node matches */
    reset_mocks();
    req.forwarded = (int8_t)0xFF;
    run_request(&req, 0x18);
    ASSERT_EQ(0x2E, mock_reply_type_of_last_send());
    ASSERT_EQ(NODE_$ME, mock_reply_node_of_last_send());

    /* a different node also takes it */
    reset_mocks();
    req.forwarded = 0;
    req.node_id = 0x00099999;
    run_request(&req, 0x18);
    ASSERT_EQ(0x2E, mock_reply_type_of_last_send());
}

/* ROUTE_$VALIDATE_PORT's answers become the reply status (0x00E65B62). */
TEST(validate_port_status_mapping)
{
    asknode_request_t req;

    memset(&req, 0, sizeof(req));
    req.version = 3;
    req.request_type = ASKNODE_REQ_WHO;
    req.node_id = 0x00077777;
    req.param1  = 0x00030000u;

    mock_validate_result = 2;
    run_request(&req, 0x18);
    ASSERT_EQ(0x11001D, mock_reply_status_of_last_send());

    reset_mocks();
    mock_validate_result = 0;
    run_request(&req, 0x18);
    ASSERT_EQ(0x110017, mock_reply_status_of_last_send());

    reset_mocks();
    mock_validate_result = 1;
    run_request(&req, 0x18);
    ASSERT_EQ(0, mock_reply_status_of_last_send());
}

/* Request 0x0E records the failure and answers nothing (0x00E65D20). */
TEST(record_failure_sends_nothing)
{
    asknode_request_t req;

    memset(&req, 0, sizeof(req));
    req.version = 3;
    req.request_type = ASKNODE_REQ_RECORD_FAILURE;
    req.node_id = 0x00066666;
    run_request(&req, 0x18);

    ASSERT_EQ(0, mock_send_calls);
    ASSERT_TRUE(NETWORK_$FAILURE_REC.flag < 0);
    ASSERT_EQ(0x000AAAAA, NETWORK_$FAILURE_REC.error_info);  /* rx node id */
    ASSERT_EQ(0x11223344, NETWORK_$FAILURE_REC.timestamp);
    ASSERT_EQ(0x00066666, NETWORK_$FAILURE_REC.node_id);
}

/* Request 0x45 answers through the context record, not the network. */
TEST(time_sync_sends_nothing)
{
    asknode_request_t req;

    memset(&req, 0, sizeof(req));
    req.version = 3;
    req.request_type = ASKNODE_REQ_TIME_SYNC;
    req.param3 = 4;
    run_request(&req, 0x18);

    ASSERT_EQ(0, mock_send_calls);
    ASSERT_EQ(1, mock_time_clock_calls);
    ASSERT_EQ(0x46, ctx.request_type);
    ASSERT_EQ(0, ctx.clock_hi);
    /* (0x80000010 & 0x7fffffff) - 4 */
    ASSERT_EQ(0x0000000C, ctx.clock_lo);
    /* propagation is unconditional here (st D2b at 0x00E65CCE) */
    ASSERT_EQ(3, ctx.version);
}

/*
 * Request 0x31 reads the log into a netbuf data page and sends it as the
 * payload; the template shrinks to 0x0A bytes (0x00E65DA0).
 */
TEST(log_read_uses_data_buffer)
{
    asknode_request_t req;

    memset(&req, 0, sizeof(req));
    req.version = 3;
    req.request_type = ASKNODE_REQ_LOG_READ;
    req.node_id = 0x02000000u;      /* high word 0x0200, bit 0 clear */
    run_request(&req, 0x18);

    ASSERT_EQ(1, mock_get_dat_calls);
    ASSERT_EQ(1, mock_log_read_calls);
    ASSERT_EQ(0, mock_log_read2_calls);
    ASSERT_EQ(0x0200, mock_log_read_len);
    ASSERT_EQ(1, mock_send_calls);
    ASSERT_EQ(0x0A, mock_send_template_len);
    ASSERT_EQ(0x30, mock_send_data_len);
    ASSERT_TRUE(mock_send_data == (void *)(uintptr_t)MOCK_LOG_VA);
    ASSERT_EQ(1, mock_rtn_dat_calls);

    /* the request length is clamped to 0x400 */
    reset_mocks();
    req.node_id = 0x09000000u;
    run_request(&req, 0x18);
    ASSERT_EQ(0x0400, mock_log_read_len);

    /* bit 0 of the high word selects LOG_$READ2, whose offset is the LOW word */
    reset_mocks();
    req.node_id = 0x00010042u;
    run_request(&req, 0x18);
    ASSERT_EQ(0, mock_log_read_calls);
    ASSERT_EQ(1, mock_log_read2_calls);
    ASSERT_EQ(0x0042, mock_log_read2_offset);
    ASSERT_EQ(0xFFFF, (uint32_t)mock_reply_status_of_last_send() & 0xFFFF);
}

/*
 * The PKT_$SEND_INTERNET argument shape at 0x00E65E26: the two output words
 * are distinct locals, argument 11 is A3 (the routing_info pointer on every
 * path but the log read), and the sockets come from the received header.
 */
TEST(send_internet_argument_shape)
{
    asknode_request_t req;

    memset(&req, 0, sizeof(req));
    req.version = 3;
    req.request_type = 0x02;
    run_request(&req, 0x18);

    ASSERT_EQ(1, mock_send_calls);
    ASSERT_EQ(0x0000ABCD, mock_send_routing_key);   /* record +0x1C */
    ASSERT_EQ(0x000AAAAA, mock_send_dest_node);     /* rx node id */
    ASSERT_EQ(0x0009, mock_send_dest_sock);         /* rx +0x12 */
    ASSERT_EQ(0, mock_send_src_node_or);            /* record +0x18 */
    ASSERT_EQ(0x00099999, mock_send_src_node);      /* rx +0x08 */
    ASSERT_EQ(4, mock_send_src_sock);
    ASSERT_EQ(0x1234, mock_send_request_id);        /* rx +0x06 */
    ASSERT_EQ(0x200, mock_send_template_len);
    ASSERT_EQ(0, mock_send_data_len);
    ASSERT_TRUE(mock_send_data == (void *)&routing_info);
    ASSERT_TRUE(mock_send_retry_hint != NULL);
    ASSERT_TRUE(mock_send_timeout_out != NULL);
    ASSERT_TRUE(mock_send_retry_hint != mock_send_timeout_out);
}

/* A failed receive returns without touching anything else. */
TEST(failed_receive_returns_early)
{
    asknode_request_t req;

    memset(&req, 0, sizeof(req));
    req.version = 3;
    req.request_type = 0x02;
    mock_receive_fails = 1;
    run_request(&req, 0x18);

    ASSERT_EQ(0, mock_dump_calls);
    ASSERT_EQ(0, mock_rtn_hdr_calls);
    ASSERT_EQ(0, mock_send_calls);
}

int main(void)
{
    uintptr_t lo;

    /*
     * app_$receive_rec_t.reply and .data hold 32-bit target virtual
     * addresses, so the mock records the server dereferences have to sit in
     * an arena the ARCH_HOST_VA_BASE round trip can reach.
     */
    lo = (uintptr_t)&mock_reply_hdr;
    if ((uintptr_t)mock_payload < lo) lo = (uintptr_t)mock_payload;
    ARCH_HOST_VA_BASE = lo - 0x1000u;

    printf("Running ASKNODE_$SERVER tests...\n\n");

    RUN_TEST(record_layouts);
    RUN_TEST(jump_table_delegate_set);
    RUN_TEST(delegated_request_calls_internet_info);
    RUN_TEST(unknown_request_type);
    RUN_TEST(dump_data_precedes_return_of_header);
    RUN_TEST(request_copy_is_clamped);
    RUN_TEST(reply_version_follows_request);
    RUN_TEST(who_request_body);
    RUN_TEST(who_request_flag_bit2_stops_propagation);
    RUN_TEST(who_request_suppressed_by_record_flag);
    RUN_TEST(who_remote_target_selection);
    RUN_TEST(validate_port_status_mapping);
    RUN_TEST(record_failure_sends_nothing);
    RUN_TEST(time_sync_sends_nothing);
    RUN_TEST(log_read_uses_data_buffer);
    RUN_TEST(send_internet_argument_shape);
    RUN_TEST(failed_receive_returns_early);

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
