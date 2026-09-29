/*
 * asknode/test/test_who_remote.c - unit tests for ASKNODE_$WHO_REMOTE
 *                                  (0x00E66334)
 *
 * These tests #include asknode/who_remote.c and drive the real function
 * through mocked callees, so what is exercised is the emitted translation and
 * not a re-implementation.
 *
 * What they pin down (bead source-ifqw):
 *
 *   - SOCK_$OPEN is given 0x00200020 (0x00E66434), not 0x00200000;
 *   - the request record at A6-0x268 is an asknode_request_t and its two
 *     variant forms sit where the image puts them: the simple form's WORD
 *     max_nodes at +0x08 (0x00E6648A / 0x00E66490) and the remote form's
 *     NODE_$ME at +0x08, ROUTE_$PORT at +0x0C, the 0xFF byte at +0x10, the
 *     max_nodes word at +0x12 and 0x4000 at +0x14 (0x00E6649E-0x00E664BC),
 *     with the target node at +0x04 (0x00E663D2 / 0x00E664B8);
 *   - a node that answers twice ENDS the listing: the scan at
 *     0x00E666EE-0x00E666F6 branches to the function's exit at 0x00E66714,
 *     not to the loop condition.
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
               (unsigned long long)(expected),                                \
               (unsigned long long)(actual), __LINE__);                       \
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
uint32_t ROUTE_$PORT;
uint32_t TIME_$CLOCKH;
uint16_t PROC1_$AS_ID;
uint32_t NETWORK_$ALLOWED_SERVICE;
uint32_t ASKNODE_$EMPTY_DATA;
MODULE_DATA_DEFINE(asknode_$data_t, ASKNODE_$DATA, 0x00E82408);
MODULE_DATA_DEFINE(sock_$data_t, SOCK_$DATA, 0x00E27510);
#include "fim/fim.h"
MODULE_DATA_DEFINE(fim_$wired_data_t, FIM_$WIRED_DATA, 0x00E21FE6);
name_$data_t NAME_$DATA;            /* NAME_$ROOT_UID lives in here */

/* ==========================================================================
 * Mock state
 * ========================================================================== */

#define MAX_SCRIPT 8

static ec_$eventcount_t mock_socket_ec;
static asknode_$reply_hdr_t mock_reply_hdr;
static asknode_who_response_t mock_payload;

static int8_t    mock_open_result;          /* < 0 = success */
static uint32_t  mock_open_flags;
static uint16_t  mock_open_sock;
static int       mock_close_calls;
static uint16_t  mock_close_sock;

static int16_t   mock_validate_result;

static int       mock_send_calls;
static status_$t mock_send_status;
static uint16_t  mock_send_timeout;
static uint8_t   mock_send_template[0x40];
static uint16_t  mock_send_template_len;
static uint8_t   mock_send_pkt_info[0x20];
static uint32_t  mock_send_dest_node;
static uint16_t  mock_send_src_sock;

static asknode_who_response_t mock_script[MAX_SCRIPT];
static int       mock_script_len;
static int       mock_script_pos;
static int       mock_wait_calls;

static void reset_mocks(void)
{
    memset(&mock_socket_ec, 0, sizeof(mock_socket_ec));
    memset(&mock_reply_hdr, 0, sizeof(mock_reply_hdr));
    memset(&mock_payload, 0, sizeof(mock_payload));
    memset(&ASKNODE_$DATA, 0, sizeof(ASKNODE_$DATA));
    memset(&SOCK_$DATA, 0, sizeof(SOCK_$DATA));
    memset(FIM_$WIRED_DATA.quit_ec, 0, sizeof(FIM_$WIRED_DATA.quit_ec));
    memset(FIM_$WIRED_DATA.quit_value, 0, sizeof(FIM_$WIRED_DATA.quit_value));
    memset(&NAME_$DATA, 0, sizeof(NAME_$DATA));
    memset(mock_script, 0, sizeof(mock_script));

    NODE_$ME = 0x00012345;
    ROUTE_$PORT = 0x00000011;
    TIME_$CLOCKH = 0x1000;
    PROC1_$AS_ID = 1;
    ASKNODE_$DATA.protocol_version = 0;
    ASKNODE_$EMPTY_DATA = 0;
    /* NETWORK_$CAPABLE_FLAGS bit 0 must be set for the function to proceed */
    NETWORK_$ALLOWED_SERVICE = 0x00010000;

    /* socket 5's entry */
    SOCK_$DATA.socket_ptr[5] = (sock_$sock_t *)&mock_socket_ec;

    /* the reply header the receive path reads */
    mock_reply_hdr.prefix.magic    = 0x0118;
    mock_reply_hdr.prefix.template_len   = sizeof(asknode_who_response_t);
    mock_reply_hdr.prefix.data_len = 0;
    mock_reply_hdr.prefix.request_id = 0x1234;   /* PKT_$NEXT_ID below */

    mock_open_result = (int8_t)0xFF;
    mock_open_flags = 0;
    mock_open_sock = 0;
    mock_close_calls = 0;
    mock_close_sock = 0;

    mock_validate_result = 1;

    mock_send_calls = 0;
    mock_send_status = 0;
    mock_send_timeout = 0;
    memset(mock_send_template, 0, sizeof(mock_send_template));
    mock_send_template_len = 0;
    memset(mock_send_pkt_info, 0, sizeof(mock_send_pkt_info));
    mock_send_dest_node = 0;
    mock_send_src_sock = 0;

    mock_script_len = 0;
    mock_script_pos = 0;
    mock_wait_calls = 0;
}

/* ==========================================================================
 * Mocked callees
 * ========================================================================== */

uint32_t DIR_$FIND_NET(uid_t *dir_uid, uint32_t *index)
{ (void)dir_uid; (void)index; return 0x00000077; }

int16_t ROUTE_$VALIDATE_PORT(int32_t routing_key, int8_t is_local)
{ (void)routing_key; (void)is_local; return mock_validate_result; }

int8_t SOCK_$OPEN(uint16_t sock_num, uint32_t proto_bufpages,
                  uint32_t max_queue)
{
    (void)max_queue;
    mock_open_sock = sock_num;
    mock_open_flags = proto_bufpages;
    return mock_open_result;
}

void SOCK_$CLOSE(uint16_t sock_num)
{ mock_close_calls++; mock_close_sock = sock_num; }

int32_t EC_$READ(ec_$eventcount_t *ec) { return (int32_t)ec->value; }

int16_t EC_$WAIT(ec_$wait_ecs_t ecs, ec_$wait_vals_t vals)
{
    (void)ecs; (void)vals;
    mock_wait_calls++;
    if (mock_script_pos < mock_script_len) {
        return 0;               /* a packet is waiting */
    }
    return 1;                   /* timeout */
}

int16_t PKT_$NEXT_ID(void) { return 0x1234; }

void PKT_$SEND_INTERNET(uint32_t routing_key, uint32_t dest_node,
                        uint16_t dest_sock, int32_t src_node_or,
                        uint32_t src_node, uint16_t src_sock,
                        void *pkt_info, uint16_t request_id,
                        void *template, uint16_t template_len,
                        void *data, int16_t data_len,
                        uint16_t *retry_hint, uint16_t *timeout_out,
                        status_$t *status_ret)
{
    (void)routing_key; (void)dest_sock; (void)src_node_or; (void)src_node;
    (void)request_id; (void)data; (void)data_len;
    mock_send_calls++;
    mock_send_dest_node = dest_node;
    mock_send_src_sock = src_sock;
    mock_send_template_len = template_len;
    if (template_len > sizeof(mock_send_template)) {
        template_len = sizeof(mock_send_template);
    }
    memcpy(mock_send_template, template, template_len);
    memcpy(mock_send_pkt_info, pkt_info, sizeof(mock_send_pkt_info));
    *retry_hint = 0;
    *timeout_out = mock_send_timeout;
    *status_ret = mock_send_status;
}

void APP_$RECEIVE(uint16_t sock_num, void *result, status_$t *status_ret)
{
    app_$receive_rec_t *r = (app_$receive_rec_t *)result;

    if (sock_num != ASKNODE_WHO_SOCKET) {
        printf("FAILED\n    APP_$RECEIVE socket %u at line %d\n",
               sock_num, __LINE__);
        tests_failed++; current_failed = 1;
    }
    if (mock_script_pos >= mock_script_len) {
        *status_ret = 0x00110006;
        return;
    }
    mock_payload = mock_script[mock_script_pos++];
    memset(r, 0, sizeof(*r));
    r->reply = ARCH_PTR_TO_VA(&mock_reply_hdr);
    r->data  = ARCH_PTR_TO_VA(&mock_payload);
    *status_ret = 0;
}

void OS_$DATA_COPY(const void *src, void *dst, uint32_t len)
{ memcpy(dst, src, len); }

void NETBUF_$RTN_HDR(uint32_t *va_ptr) { (void)va_ptr; }

void PKT_$DUMP_DATA(uint32_t *buffers, int16_t len)
{ (void)buffers; (void)len; }

/* ==========================================================================
 * The translation unit under test
 * ========================================================================== */

#include "../who_remote.c"

/* ==========================================================================
 * Helpers
 * ========================================================================== */

static int32_t   node_list[16];
static uint16_t  out_count;
static status_$t out_status;

static void run(int32_t node, int32_t port, int16_t max_count)
{
    int32_t  n = node;
    int32_t  p = port;
    int16_t  m = max_count;

    memset(node_list, 0, sizeof(node_list));
    out_count = 0xFFFF;
    out_status = 0xDEADBEEF;
    ASKNODE_$WHO_REMOTE(&n, &p, node_list, &m, &out_count, &out_status);
}

static uint16_t tpl_w(unsigned off)
{
    uint16_t v;
    memcpy(&v, mock_send_template + off, sizeof(v));
    return v;
}

static uint32_t tpl_l(unsigned off)
{
    uint32_t v;
    memcpy(&v, mock_send_template + off, sizeof(v));
    return v;
}

/* Queue one sequential (non-indexed) WHO answer. */
static void script_node(uint32_t node_id)
{
    asknode_who_response_t *r = &mock_script[mock_script_len++];
    memset(r, 0, sizeof(*r));
    r->version = 3;
    r->response_type = 1;
    r->status = 0;
    r->node_id = node_id;
    r->flags = 0;               /* not 0xB1FF: the sequential form */
    r->count = 0;
}

/* ==========================================================================
 * Tests
 * ========================================================================== */

/* 0x00E66434: the socket flags longword. */
TEST(socket_open_flags)
{
    run(0, 0x55, 4);
    ASSERT_EQ(ASKNODE_WHO_SOCKET, mock_open_sock);
    ASSERT_EQ(0x00200020u, mock_open_flags);
}

/* 0x00E6644C: a socket already in use ends the call. */
TEST(socket_open_failure_is_reported)
{
    mock_open_result = 0;
    run(0, 0x55, 4);
    ASSERT_EQ(status_$network_conflict_with_another_node_listing, out_status);
    ASSERT_EQ(0, mock_send_calls);
    ASSERT_EQ(0, mock_close_calls);
}

/*
 * 0x00E66472 - 0x00E6648E: the LOCAL simple form.  The request type word is
 * zero, the max-node count is a WORD at +0x08 and it is one less than the
 * caller asked for because the local node is already in the list.  The
 * destination node becomes 2 and the packet length 0x90.
 */
TEST(local_request_is_the_simple_form)
{
    run(0, -1, 4);

    ASSERT_EQ(1, mock_send_calls);
    ASSERT_EQ(0x18, mock_send_template_len);
    ASSERT_EQ(3, tpl_w(0x00));                  /* protocol version */
    ASSERT_EQ(ASKNODE_REQ_WHO, tpl_w(0x02));    /* 0 */
    ASSERT_EQ(0x00012345u, tpl_l(0x04));        /* NODE_$ME */
    ASSERT_EQ(3, tpl_w(0x08));                  /* max_nodes - 1, a WORD */
    ASSERT_EQ(2u, mock_send_dest_node);
    {
        uint16_t len;
        memcpy(&len, mock_send_pkt_info, sizeof(len));
        ASSERT_EQ(0x90, len);
    }
    /* the local node is already listed */
    ASSERT_EQ((int32_t)0x00012345, node_list[0]);
}

/*
 * 0x00E66490: a remote node reached over our own port still uses the simple
 * form, but the word at +0x08 is the full count.
 */
TEST(direct_remote_request_is_the_simple_form_with_the_full_count)
{
    run(0x00099999, (int32_t)0x00000011 /* == ROUTE_$PORT */, 4);

    ASSERT_EQ(1, mock_send_calls);
    ASSERT_EQ(ASKNODE_REQ_WHO, tpl_w(0x02));
    ASSERT_EQ(4, tpl_w(0x08));
    ASSERT_EQ(0x00099999u, mock_send_dest_node);
    {
        uint16_t len;
        memcpy(&len, mock_send_pkt_info, sizeof(len));
        ASSERT_EQ(0x10, len);
    }
}

/*
 * 0x00E66498 - 0x00E664BC: the REMOTE form.  Every field is one slot lower
 * and narrower than the tree used to write it.
 */
TEST(remote_request_record_field_offsets)
{
    run(0x00099999, 0x00000077, 5);

    ASSERT_EQ(1, mock_send_calls);
    ASSERT_EQ(0x18, mock_send_template_len);
    ASSERT_EQ(3, tpl_w(0x00));                        /* version */
    ASSERT_EQ(ASKNODE_REQ_WHO_REMOTE, tpl_w(0x02));   /* 0x2D */
    ASSERT_EQ(0x00099999u, tpl_l(0x04));              /* the queried node */
    ASSERT_EQ(0x00012345u, tpl_l(0x08));              /* NODE_$ME */
    ASSERT_EQ(0x00000011u, tpl_l(0x0C));              /* ROUTE_$PORT */
    ASSERT_EQ(0xFF, mock_send_template[0x10]);        /* "st", a BYTE */
    ASSERT_EQ(5, tpl_w(0x12));                        /* max_nodes, a WORD */
    ASSERT_EQ(0x4000u, tpl_l(0x14));
}

/* 0x00E663BC: the protocol version word is 2 when the global reads 3. */
TEST(protocol_version_word)
{
    ASKNODE_$DATA.protocol_version = 3;
    run(0x00099999, 0x00000077, 5);
    ASSERT_EQ(2, tpl_w(0x00));
}

/* 0x00E66418: hardware that cannot carry the request. */
TEST(validate_port_result_2_is_refused)
{
    mock_validate_result = 2;
    run(0x00099999, 0x00000077, 5);
    ASSERT_EQ(status_$network_operation_not_defined_on_hardware, out_status);
    ASSERT_EQ(0, mock_send_calls);
}

/*
 * 0x00E666E4 - 0x00E6670A: the duplicate-node branch goes to the function's
 * EXIT.  Two distinct nodes answer and are listed, then the first one answers
 * again: the listing stops there with a good status.  Had the duplicate
 * merely been skipped, the loop would have run on to the timeout and
 * returned 0x0011001B instead.
 */
TEST(a_repeated_node_ends_the_listing)
{
    script_node(0x000AAAAA);
    script_node(0x000BBBBB);
    script_node(0x000AAAAA);        /* the duplicate */
    script_node(0x000CCCCC);        /* never reached */

    run(0, -1, 6);

    ASSERT_EQ(0u, out_status);
    ASSERT_EQ(3, out_count);
    ASSERT_EQ((int32_t)0x00012345, node_list[0]);   /* NODE_$ME */
    ASSERT_EQ((int32_t)0x000AAAAA, node_list[1]);
    ASSERT_EQ((int32_t)0x000BBBBB, node_list[2]);
    ASSERT_EQ(0, node_list[3]);                     /* 0x000CCCCC never read */
    /* the fourth scripted answer was left in the queue */
    ASSERT_EQ(3, mock_script_pos);
    ASSERT_EQ(1, mock_close_calls);
    ASSERT_EQ(ASKNODE_WHO_SOCKET, mock_close_sock);
}

/*
 * The same script without the repeat runs to the timeout, which is what the
 * old "skip the duplicate" reading would have produced above.
 */
TEST(distinct_nodes_run_on_to_the_timeout)
{
    script_node(0x000AAAAA);
    script_node(0x000BBBBB);

    run(0, -1, 6);

    ASSERT_EQ(status_$network_waited_too_long_for_more_node_responses,
              out_status);
    ASSERT_EQ(3, out_count);
    ASSERT_EQ((int32_t)0x000AAAAA, node_list[1]);
    ASSERT_EQ((int32_t)0x000BBBBB, node_list[2]);
}

/*
 * 0x00E6669E - 0x00E666B0: our own node answering a type-1 reply also ends
 * the listing - that is how the broadcast form knows it has been all the way
 * round.
 */
TEST(our_own_node_answering_ends_the_listing)
{
    script_node(0x000AAAAA);
    script_node(NODE_$ME);
    script_node(0x000BBBBB);        /* never reached */

    run(0, -1, 6);

    ASSERT_EQ(0u, out_status);
    ASSERT_EQ(2, out_count);
    ASSERT_EQ(2, mock_script_pos);
}

/* 0x00E663A8: a caller that already has enough nodes never opens a socket. */
TEST(a_full_list_returns_before_opening_the_socket)
{
    run(0, -1, 1);
    ASSERT_EQ(1, out_count);
    ASSERT_EQ(0u, out_status);
    ASSERT_EQ(0, mock_open_sock);
    ASSERT_EQ(0, mock_send_calls);
}

int main(void)
{
    uintptr_t lo;

    /*
     * app_$receive_rec_t.reply and .data hold 32-bit target virtual
     * addresses, so the mock records this function dereferences have to sit
     * in an arena the ARCH_HOST_VA_BASE round trip can reach.
     */
    lo = (uintptr_t)&mock_reply_hdr;
    if ((uintptr_t)&mock_payload < lo) lo = (uintptr_t)&mock_payload;
    ARCH_HOST_VA_BASE = lo - 0x1000u;

    printf("ASKNODE_$WHO_REMOTE tests\n");
    RUN_TEST(socket_open_flags);
    RUN_TEST(socket_open_failure_is_reported);
    RUN_TEST(local_request_is_the_simple_form);
    RUN_TEST(direct_remote_request_is_the_simple_form_with_the_full_count);
    RUN_TEST(remote_request_record_field_offsets);
    RUN_TEST(protocol_version_word);
    RUN_TEST(validate_port_result_2_is_refused);
    RUN_TEST(a_repeated_node_ends_the_listing);
    RUN_TEST(distinct_nodes_run_on_to_the_timeout);
    RUN_TEST(our_own_node_answering_ends_the_listing);
    RUN_TEST(a_full_list_returns_before_opening_the_socket);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
