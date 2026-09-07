/*
 * pkt/test/test_likely_to_answer.c - Unit tests for PKT_$LIKELY_TO_ANSWER
 * (0x00E1299E).
 *
 * The test compiles the real pkt/likely_to_answer.c and pkt/pkt_data.c and
 * supplies scripted versions of everything the function calls, so each of the
 * original's paths (no route, indirect route, wrong port type, socket
 * exhaustion, ping answered, ping unanswered) can be driven and the fifteen
 * PKT_$SEND_INTERNET arguments checked one by one.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ==========================================================================
 * Test framework
 * ========================================================================== */

static int tests_failed = 0;
static int tests_run = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name)                                                        \
    do {                                                                      \
        printf("  Running %s... ", #name);                                    \
        tests_run++;                                                          \
        test_##name();                                                        \
        printf("done\n");                                                     \
    } while (0)

#define ASSERT_EQ(expected, actual)                                           \
    do {                                                                      \
        long long _e = (long long)(expected);                                 \
        long long _a = (long long)(actual);                                   \
        if (_e != _a) {                                                       \
            printf("FAILED\n    Expected: 0x%llx, Got: 0x%llx at line %d\n",  \
                   (unsigned long long)_e, (unsigned long long)_a, __LINE__); \
            tests_failed++;                                                   \
            return;                                                           \
        }                                                                     \
    } while (0)

#include "pkt/pkt_internal.h"

/* ==========================================================================
 * Globals and scripted callees
 * ========================================================================== */

uint32_t NODE_$ME;
uint32_t TIME_$CLOCKH;
uint8_t sock_table_base[SOCK_TABLE_SIZE];
route_$port_t *ROUTE_$PORTP[ROUTE_$MAX_PORTS];
int8_t NETWORK_$LOOPBACK_FLAG;

/* --- RIP_$FIND_NEXTHOP ---------------------------------------------------- */
static int16_t rip_result;
static status_$t rip_status;
static int16_t rip_port;
static rip_$dest_addr_t rip_seen_dest;
static boolean rip_seen_flags;

int16_t RIP_$FIND_NEXTHOP(void *addr_info, boolean flags, int16_t *port_ret,
                          void *nexthop_ret, status_$t *status_ret)
{
    rip_seen_dest = *(rip_$dest_addr_t *)addr_info;
    rip_seen_flags = flags;
    memcpy(nexthop_ret, addr_info, sizeof(rip_$nexthop_t));
    *port_ret = rip_port;
    *status_ret = rip_status;
    return rip_result;
}

/* --- PKT_$RECENTLY_MISSING / PKT_$NOTE_VISIBLE ---------------------------- */
static boolean recently_missing;
static uint32_t recently_missing_node;
static int note_visible_calls;
static uint32_t note_visible_node;
static boolean note_visible_flag;

boolean PKT_$RECENTLY_MISSING(uint32_t node_id)
{
    recently_missing_node = node_id;
    return recently_missing;
}

void PKT_$NOTE_VISIBLE(uint32_t node_id, boolean is_visible)
{
    note_visible_calls++;
    note_visible_node = node_id;
    note_visible_flag = is_visible;
}

/* --- SOCK_$ALLOCATE / SOCK_$CLOSE ----------------------------------------- */
static int8_t sock_alloc_result;
static uint16_t sock_alloc_num;
static uint32_t sock_alloc_proto;
static uint32_t sock_alloc_queue;
static int sock_close_calls;
static uint16_t sock_close_num;

int8_t SOCK_$ALLOCATE(uint16_t *sock_ret, uint32_t proto_bufpages,
                      uint32_t max_queue)
{
    sock_alloc_proto = proto_bufpages;
    sock_alloc_queue = max_queue;
    *sock_ret = sock_alloc_num;
    return sock_alloc_result;
}

void SOCK_$CLOSE(uint16_t sock_num)
{
    sock_close_calls++;
    sock_close_num = sock_num;
}

/* --- PKT_$NEXT_ID --------------------------------------------------------- */
static int16_t next_id_value;

int16_t PKT_$NEXT_ID(void) { return next_id_value; }

/* --- PKT_$SEND_INTERNET --------------------------------------------------- */
struct send_args {
    uint32_t routing_key;
    uint32_t dest_node;
    uint16_t dest_sock;
    int32_t src_node_or;
    uint32_t src_node;
    uint16_t src_sock;
    void *pkt_info;
    uint16_t request_id;
    void *template;
    uint16_t template_len;
    void *data;
    int16_t data_len;
    uint16_t *retry_hint;
    uint16_t *timeout_out;
};
static struct send_args last_send;
static int send_calls;
static status_$t send_status;
static uint16_t send_retry_hint_out;
static uint16_t send_timeout_out;

void PKT_$SEND_INTERNET(uint32_t routing_key, uint32_t dest_node,
                        uint16_t dest_sock, int32_t src_node_or,
                        uint32_t src_node, uint16_t src_sock, void *pkt_info,
                        uint16_t request_id, void *template,
                        uint16_t template_len, void *data, int16_t data_len,
                        uint16_t *retry_hint, uint16_t *timeout_out,
                        status_$t *status_ret)
{
    send_calls++;
    last_send = (struct send_args){ routing_key, dest_node, dest_sock,
                                    src_node_or, src_node,  src_sock,
                                    pkt_info,    request_id, template,
                                    template_len, data,     data_len,
                                    retry_hint,  timeout_out };
    /* PKT_$BLD_INTERNET_HDR stores 5 and 4 here (0x00E1230E / 0x00E1231A). */
    *retry_hint = send_retry_hint_out;
    *timeout_out = send_timeout_out;
    *status_ret = send_status;
}

/* --- EC_$WAIT ------------------------------------------------------------- */
/* Scripted: ec_wait_script[n] is the index returned by the n'th call. */
static int16_t ec_wait_script[16];
static int ec_wait_calls;
static ec_$wait_ecs_t last_ecs;
static ec_$wait_vals_t last_vals;

int16_t EC_$WAIT(ec_$wait_ecs_t ecs, ec_$wait_vals_t vals)
{
    last_ecs = ecs;
    last_vals = vals;
    if (ec_wait_calls >= (int)(sizeof(ec_wait_script) / sizeof(ec_wait_script[0]))) {
        return 1;
    }
    return ec_wait_script[ec_wait_calls++];
}

/* --- APP_$RECEIVE --------------------------------------------------------- */
static pkt_$internet_hdr_t recv_hdr;
static app_$receive_rec_t recv_template;
static status_$t recv_status;
static int recv_calls;
static uint16_t recv_sock;

void APP_$RECEIVE(uint16_t sock_num, void *result, status_$t *status_ret)
{
    recv_calls++;
    recv_sock = sock_num;
    memcpy(result, &recv_template, sizeof(recv_template));
    *status_ret = recv_status;
}

/* --- NETBUF_$RTN_HDR / PKT_$DUMP_DATA ------------------------------------- */
static int rtn_hdr_calls;
static uint32_t rtn_hdr_value;
static int dump_data_calls;
static int16_t dump_data_len;
static uint32_t *dump_data_bufs;

void NETBUF_$RTN_HDR(uint32_t *ppn)
{
    rtn_hdr_calls++;
    rtn_hdr_value = *ppn;
}

void PKT_$DUMP_DATA(uint32_t *buffers, int16_t len)
{
    dump_data_calls++;
    dump_data_bufs = buffers;
    dump_data_len = len;
}

#include "../pkt_data.c"
#include "../likely_to_answer.c"

/* ==========================================================================
 * Fixture
 * ========================================================================== */

#define TEST_NETWORK 0x11223344u
#define TEST_NODE    0x000ABCDEu
#define TEST_SOCK    7
#define TEST_REQ_ID  0x1234

static route_$port_t test_port;
static pkt_$net_addr_t addr;
static sock_$sock_t test_socket_desc;

static void reset_state(void)
{
    memset(&test_port, 0, sizeof(test_port));
    memset(ROUTE_$PORTP, 0, sizeof(ROUTE_$PORTP));
    memset(sock_table_base, 0, sizeof(sock_table_base));
    memset(&test_socket_desc, 0, sizeof(test_socket_desc));
    memset(&recv_hdr, 0, sizeof(recv_hdr));
    memset(&recv_template, 0, sizeof(recv_template));
    memset(ec_wait_script, 0, sizeof(ec_wait_script));
    memset(&last_send, 0, sizeof(last_send));

    addr.network = TEST_NETWORK;
    addr.node = TEST_NODE;

    NODE_$ME = 0x000FEDCB;
    TIME_$CLOCKH = 0x1000;

    rip_result = 0;
    rip_status = status_$ok;
    rip_port = 3;
    ROUTE_$PORTP[3] = &test_port;
    test_port.port_type = 4;

    recently_missing = false;
    note_visible_calls = 0;
    note_visible_flag = 0x7F;

    sock_alloc_result = (int8_t)0xFF;   /* negative == allocated */
    sock_alloc_num = TEST_SOCK;
    sock_close_calls = 0;

    next_id_value = TEST_REQ_ID;

    send_calls = 0;
    send_status = status_$ok;
    send_retry_hint_out = 5;
    send_timeout_out = 4;

    ec_wait_calls = 0;

    recv_status = status_$ok;
    recv_calls = 0;
    recv_hdr.data_len = 0x20;
    recv_hdr.request_id = TEST_REQ_ID;
    recv_template.reply = ARCH_PTR_TO_VA(&recv_hdr);
    recv_template.data = 0x0004AB57;
    recv_template.data_pages[0] = 0;

    rtn_hdr_calls = 0;
    dump_data_calls = 0;

    SOCK_$EVENT_COUNTERS[TEST_SOCK - 1] = &test_socket_desc.ec;
    test_socket_desc.ec.value = 40;
}

/* ==========================================================================
 * Tests
 * ========================================================================== */

/* 0x00E129B4 - 0x00E129DA: the destination record handed to RIP. */
TEST(builds_the_rip_destination_record)
{
    status_$t status = status_$ok;

    reset_state();
    PKT_$LIKELY_TO_ANSWER(&addr, &status);

    ASSERT_EQ(TEST_NETWORK, rip_seen_dest.network);
    /* Only the low 20 bits of host_lo are written (0x00E129B8 masks with
     * 0xFFF00000 and 0x00E129C4 ORs the node in). */
    ASSERT_EQ(TEST_NODE, rip_seen_dest.host_lo & 0x000FFFFFu);
    ASSERT_EQ(false, rip_seen_flags);   /* 0x00E129D4 "clr.w -(SP)" */
}

/* 0x00E129E8 - 0x00E129EE: a failed lookup returns false and leaves the
 * status alone. */
TEST(route_lookup_failure_returns_false)
{
    status_$t status;

    reset_state();
    rip_status = 0x00110008;    /* unable to route */
    status = status_$ok;

    ASSERT_EQ(0, PKT_$LIKELY_TO_ANSWER(&addr, &status));
    ASSERT_EQ(0x00110008, status);
    ASSERT_EQ(0, send_calls);
    ASSERT_EQ(0, note_visible_calls);
}

/*
 * 0x00E129F4 / 0x00E12A0A: no ping for an indirect route or a port whose
 * type is not 4; the answer is then ~PKT_$RECENTLY_MISSING and the failure
 * status is set unconditionally (0x00E12BA2).
 */
TEST(no_ping_paths_use_the_missing_list)
{
    status_$t status;

    /* Indirect route. */
    reset_state();
    rip_result = 2;
    recently_missing = false;
    status = status_$ok;
    ASSERT_EQ((int8_t)0xFF, PKT_$LIKELY_TO_ANSWER(&addr, &status));
    ASSERT_EQ(status_$network_remote_node_failed_to_respond, status);
    ASSERT_EQ(TEST_NODE, recently_missing_node);
    ASSERT_EQ(0, send_calls);
    ASSERT_EQ(0, note_visible_calls);

    /* Indirect route, node is on the missing list -> false. */
    reset_state();
    rip_result = 2;
    recently_missing = true;
    status = status_$ok;
    ASSERT_EQ(0, PKT_$LIKELY_TO_ANSWER(&addr, &status));
    ASSERT_EQ(status_$network_remote_node_failed_to_respond, status);

    /* Direct route but the port is not type 4. */
    reset_state();
    test_port.port_type = 3;
    recently_missing = true;
    status = status_$ok;
    ASSERT_EQ(0, PKT_$LIKELY_TO_ANSWER(&addr, &status));
    ASSERT_EQ(0, send_calls);
}

/*
 * 0x00E12A2E - 0x00E12A54: SOCK_$ALLOCATE's arguments, and the fact that a
 * failure returns D2 - which is still the "true" stored at 0x00E129F2.
 */
TEST(socket_allocation_failure)
{
    status_$t status = status_$ok;

    reset_state();
    sock_alloc_result = 0;      /* non-negative == failed */

    ASSERT_EQ((int8_t)0xFF, PKT_$LIKELY_TO_ANSWER(&addr, &status));
    ASSERT_EQ(status_$network_no_more_free_sockets, status);
    ASSERT_EQ(0x20001, sock_alloc_proto);
    ASSERT_EQ(PKT_CHUNK_SIZE, sock_alloc_queue);
    ASSERT_EQ(0, send_calls);
    ASSERT_EQ(0, sock_close_calls);
}

/* 0x00E12A84 - 0x00E12ABE: every one of the fifteen arguments. */
TEST(send_internet_argument_list)
{
    status_$t status = status_$ok;

    reset_state();
    ec_wait_script[0] = 0;      /* socket signalled -> receive, id matches */

    PKT_$LIKELY_TO_ANSWER(&addr, &status);

    ASSERT_EQ(1, send_calls);
    ASSERT_EQ(TEST_NETWORK, last_send.routing_key);
    /* 0x00E12AB8 pushes the RAW node longword, not the 20-bit masked one. */
    ASSERT_EQ(TEST_NODE, last_send.dest_node);
    ASSERT_EQ(0x0D, last_send.dest_sock);
    ASSERT_EQ(-1, last_send.src_node_or);
    ASSERT_EQ(NODE_$ME, last_send.src_node);
    ASSERT_EQ(TEST_SOCK, last_send.src_sock);
    ASSERT_EQ((uintptr_t)&PKT_$DATA->ping_template, (uintptr_t)last_send.pkt_info);
    ASSERT_EQ(TEST_REQ_ID, last_send.request_id);
    ASSERT_EQ((uintptr_t)&PKT_$DATA->ping_req_hdr, (uintptr_t)last_send.template);
    ASSERT_EQ(2, last_send.template_len);
    ASSERT_EQ((uintptr_t)&pkt_$no_data, (uintptr_t)last_send.data);
    ASSERT_EQ(0, last_send.data_len);
    /* retry_hint and timeout_out are two DIFFERENT locals (A6-0x68 and
     * A6-0x66); neither may be NULL. */
    ASSERT_EQ(1, last_send.retry_hint != NULL);
    ASSERT_EQ(1, last_send.timeout_out != NULL);
    ASSERT_EQ(1, last_send.retry_hint != last_send.timeout_out);
}

/*
 * 0x00E12AC6 - 0x00E12ADC: the deadline comes from the *timeout_out* word,
 * zero-extended, plus TIME_$CLOCKH plus one - not from retry_hint.
 */
TEST(deadline_comes_from_the_timeout_out_word)
{
    status_$t status = status_$ok;

    reset_state();
    TIME_$CLOCKH = 0x40000000;
    send_retry_hint_out = 5;
    send_timeout_out = 0xFFFF;
    ec_wait_script[0] = 1;      /* time out immediately, three sends */

    PKT_$LIKELY_TO_ANSWER(&addr, &status);

    ASSERT_EQ((int32_t)(0x40000000 + 0xFFFF + 1), last_vals.val[1]);
}

/* 0x00E12B42 - 0x00E12B5E: the EC_$WAIT argument arrays. */
TEST(ec_wait_argument_build)
{
    status_$t status = status_$ok;

    reset_state();
    test_socket_desc.ec.value = 40;
    ec_wait_script[0] = 1;

    PKT_$LIKELY_TO_ANSWER(&addr, &status);

    ASSERT_EQ((uintptr_t)&test_socket_desc.ec, (uintptr_t)last_ecs.ec[0]);
    ASSERT_EQ((uintptr_t)&TIME_$CLOCKH, (uintptr_t)last_ecs.ec[1]);
    ASSERT_EQ(0, (uintptr_t)last_ecs.ec[2]);   /* 0x00E12A7E "movea.l #0x0,A3" */
    ASSERT_EQ(41, last_vals.val[0]);           /* ec.value + 1 */
    ASSERT_EQ(1, last_vals.val[2]);            /* 0x00E12B42 "pea (0x1).w" */
}

/*
 * The answered case: EC_$WAIT reports the socket, APP_$RECEIVE hands back a
 * reply whose id matches, and the function reports success.
 */
TEST(ping_answered)
{
    status_$t status = status_$ok;

    reset_state();
    ec_wait_script[0] = 0;
    recv_template.data = 0x0004AB57;
    recv_template.data_pages[0] = 0x1234;

    ASSERT_EQ((int8_t)0xFF, PKT_$LIKELY_TO_ANSWER(&addr, &status));
    ASSERT_EQ(status_$ok, status);          /* 0x00E12BA0 skips the 0x110007 */
    ASSERT_EQ(1, send_calls);
    ASSERT_EQ(1, recv_calls);
    ASSERT_EQ(TEST_SOCK, recv_sock);

    /* 0x00E12B10 "andi.w #-0x400,D0w" only clears bits 0..9. */
    ASSERT_EQ(1, rtn_hdr_calls);
    ASSERT_EQ(0x0004A800, rtn_hdr_value);
    ASSERT_EQ(0x0004AB57, recv_template.data); /* the record is untouched */

    /* 0x00E12B24: only dumped when the first buffer slot is non-zero, and
     * the length is the header's data_len word. */
    ASSERT_EQ(1, dump_data_calls);
    ASSERT_EQ(0x20, dump_data_len);

    ASSERT_EQ(1, sock_close_calls);
    ASSERT_EQ(TEST_SOCK, sock_close_num);
    ASSERT_EQ(1, note_visible_calls);
    ASSERT_EQ(TEST_NODE, note_visible_node);
    ASSERT_EQ((int8_t)0xFF, note_visible_flag);
}

/* 0x00E12B24: a zero first data-buffer slot skips PKT_$DUMP_DATA. */
TEST(no_data_buffers_skips_dump)
{
    status_$t status = status_$ok;

    reset_state();
    ec_wait_script[0] = 0;
    recv_template.data_pages[0] = 0;

    PKT_$LIKELY_TO_ANSWER(&addr, &status);
    ASSERT_EQ(0, dump_data_calls);
    ASSERT_EQ(1, rtn_hdr_calls);
}

/*
 * 0x00E12B3A: a reply carrying somebody else's id does not count; the loop
 * goes back to the EC_$WAIT without re-sending.
 */
TEST(mismatched_reply_id_keeps_waiting)
{
    status_$t status = status_$ok;

    reset_state();
    recv_hdr.request_id = TEST_REQ_ID + 1;
    ec_wait_script[0] = 0;      /* first wait: a packet */
    ec_wait_script[1] = 1;      /* second wait: timeout */
    ec_wait_script[2] = 1;
    ec_wait_script[3] = 1;

    ASSERT_EQ(0, PKT_$LIKELY_TO_ANSWER(&addr, &status));
    ASSERT_EQ(1, recv_calls);
    /* Three sends: "moveq #0x2,D5" plus "dbf" == three passes. */
    ASSERT_EQ(3, send_calls);
    ASSERT_EQ(status_$network_remote_node_failed_to_respond, status);
    ASSERT_EQ((int8_t)0, note_visible_flag);
}

/* 0x00E12A7A / 0x00E12B76: the dbf counter gives exactly three sends. */
TEST(unanswered_ping_sends_three_times)
{
    status_$t status = status_$ok;

    reset_state();
    ec_wait_script[0] = 1;
    ec_wait_script[1] = 1;
    ec_wait_script[2] = 1;
    ec_wait_script[3] = 1;

    ASSERT_EQ(0, PKT_$LIKELY_TO_ANSWER(&addr, &status));
    ASSERT_EQ(3, send_calls);
    ASSERT_EQ(3, ec_wait_calls);
    ASSERT_EQ(0, recv_calls);
    ASSERT_EQ(status_$network_remote_node_failed_to_respond, status);
    ASSERT_EQ(1, sock_close_calls);
}

/* 0x00E12ACC: a send that reports an error abandons both loops at once and
 * the failure status is NOT overwritten by 0x110007. */
TEST(send_failure_aborts_immediately)
{
    status_$t status = status_$ok;

    reset_state();
    send_status = status_$network_message_header_too_big;

    ASSERT_EQ(0, PKT_$LIKELY_TO_ANSWER(&addr, &status));
    ASSERT_EQ(1, send_calls);
    ASSERT_EQ(0, ec_wait_calls);
    ASSERT_EQ(status_$network_message_header_too_big, status);
    ASSERT_EQ(1, sock_close_calls);
    ASSERT_EQ(1, note_visible_calls);
}

/* 0x00E12AFC: a receive error falls to the response test, which retries. */
TEST(receive_failure_retries)
{
    status_$t status = status_$ok;

    reset_state();
    recv_status = status_$network_buffer_queue_is_empty;
    ec_wait_script[0] = 0;
    ec_wait_script[1] = 0;
    ec_wait_script[2] = 0;

    ASSERT_EQ(0, PKT_$LIKELY_TO_ANSWER(&addr, &status));
    ASSERT_EQ(3, send_calls);
    ASSERT_EQ(3, recv_calls);
    /* status_ret is non-zero on exit, so 0x110007 is not applied. */
    ASSERT_EQ(status_$network_buffer_queue_is_empty, status);
}

int main(void)
{
    /*
     * app_$receive_rec_t.reply holds a 32-bit target virtual address, so the
     * mock header has to sit in an arena the round trip can reach.  The .data
     * field is a literal VA the test picks, so it needs no arena.
     */
    ARCH_HOST_VA_BASE = (uintptr_t)&recv_hdr - 0x1000u;

    printf("PKT_$LIKELY_TO_ANSWER tests\n");

    RUN_TEST(builds_the_rip_destination_record);
    RUN_TEST(route_lookup_failure_returns_false);
    RUN_TEST(no_ping_paths_use_the_missing_list);
    RUN_TEST(socket_allocation_failure);
    RUN_TEST(send_internet_argument_list);
    RUN_TEST(deadline_comes_from_the_timeout_out_word);
    RUN_TEST(ec_wait_argument_build);
    RUN_TEST(ping_answered);
    RUN_TEST(no_data_buffers_skips_dump);
    RUN_TEST(mismatched_reply_id_keeps_waiting);
    RUN_TEST(unanswered_ping_sends_three_times);
    RUN_TEST(send_failure_aborts_immediately);
    RUN_TEST(receive_failure_retries);

    printf("%d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed != 0;
}
