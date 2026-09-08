/*
 * rip/test/test_init.c - unit tests for RIP_$INIT (0x00E2FBD0)
 *
 * Covers what the 2026-09-07 re-emission corrected (bead source-bkr7):
 *   - the APP_$RECEIVE record is app_$receive_rec_t: the route port is the
 *     longword at +0x18 (0x00E2FD16), the VA that NETBUF_$RTN_HDR is given is
 *     +0x04 masked to its 1KB page (0x00E2FD24-0x00E2FD30) and the page
 *     vector PKT_$DUMP_DATA is given is +0x08 (0x00E2FD48/0x00E2FD54)
 *   - PKT_$SEND_INTERNET's template is the cell at 0x00E3502C and its data
 *     pointer the cell at 0x00E2FDEC - neither is NULL
 *   - the socket table index, SOCK_$EVENT_COUNTERS[sock - 1] (0x00E2FC64)
 *   - a non-diskless node does nothing at all past the three locks
 *   - the two RIP_$UPDATE_INT calls differ only in the trailing boolean
 */

#include <stdio.h>
#include <string.h>

#include "rip/rip_internal.h"
#include "arch/arch.h"
#include "sock/sock.h"
#include "app/app.h"

/* ============================================================================
 * Test framework
 * ============================================================================ */

static int tests_run = 0;
static int tests_failed = 0;
static int current_failed = 0;

#define TEST(name)      static void test_##name(void)
#define RUN_TEST(name)  do {                                                  \
        printf("  %-52s ", #name);                                            \
        current_failed = 0;                                                   \
        tests_run++;                                                          \
        test_##name();                                                        \
        if (current_failed == 0) { printf("PASSED\n"); }                      \
    } while (0)

#define ASSERT_EQ(expected, actual) do {                                      \
        unsigned long _e = (unsigned long)(expected);                         \
        unsigned long _a = (unsigned long)(actual);                           \
        if (_e != _a) {                                                       \
            if (current_failed == 0) { printf("FAILED\n"); }                  \
            printf("      line %d: expected 0x%lx, got 0x%lx\n",              \
                   __LINE__, _e, _a);                                         \
            current_failed = 1; tests_failed++;                               \
            return;                                                           \
        }                                                                     \
    } while (0)

/* ============================================================================
 * The code under test
 * ============================================================================ */

#include "../init.c"

/* ============================================================================
 * Globals the unit reads
 * ============================================================================ */

rip_$data_t RIP_$DATA;
int8_t      NETWORK_$DISKLESS;
uint32_t    NETWORK_$MOTHER_NODE;
uint32_t    NODE_$ME;
uint32_t    TIME_$CLOCKH;
uint32_t    ROUTE_$PORT;
uint8_t     sock_table_base[SOCK_TABLE_SIZE];

/* ============================================================================
 * Mocks
 * ============================================================================ */

static ec_$eventcount_t mock_sock_ec;
static ec_$eventcount_t mock_bad_ec;      /* the decoy at [sock] */

static int      excl_init_calls;
static void    *excl_init_args[4];

static int8_t   mock_alloc_result = -1;
static uint16_t mock_alloc_sock = 7;
static status_$t mock_send_status;
static uint16_t mock_send_timeout;

/* what PKT_$SEND_INTERNET saw */
static void    *snd_template;
static uint16_t snd_template_len;
static void    *snd_data;
static int16_t  snd_data_len;
static uint32_t snd_dest_node;
static uint16_t snd_dest_sock;
static uint8_t  snd_pkt_info[30];
static int      snd_calls;

#define MAX_WAIT 8
static int16_t  wait_script[MAX_WAIT];
static int      wait_script_len;
static int      wait_calls;

static int16_t  mock_reply_id;
static uint32_t mock_route_port;
static uint32_t mock_bulk_handle;
static status_$t mock_recv_status;
static int      recv_calls;

static int      rtn_hdr_calls;
static uint32_t rtn_hdr_last;
static int      dump_calls;
static uint32_t *dump_last_pages;
static int      close_calls;
static uint16_t closed_sock;

static int      update_calls;
static uint32_t update_network[2];
static void    *update_source[2];
static boolean  update_flags[2];

/* The record RIP_$INIT reads through app_$receive_rec_t.reply, plus the
 * payload it page-aligns, live in one arena so ARCH_PTR_TO_VA fits in 32
 * bits on a 64-bit host. */
static struct {
    uint8_t                 pad[0x400];
    rip_$init_reply_hdr_t   reply_hdr;
    uint8_t                 payload[0x400];
} va_arena;

void ML_$EXCLUSION_INIT(ml_$exclusion_t *lock)
{
    if (excl_init_calls < 4) {
        excl_init_args[excl_init_calls] = lock;
    }
    excl_init_calls++;
}

int8_t SOCK_$ALLOCATE(uint16_t *sock_ret, uint32_t proto_bufpages,
                      uint32_t max_queue)
{
    (void)proto_bufpages; (void)max_queue;
    *sock_ret = mock_alloc_sock;
    return mock_alloc_result;
}

void SOCK_$CLOSE(uint16_t sock_num) { close_calls++; closed_sock = sock_num; }

int32_t EC_$READ(ec_$eventcount_t *ec) { return ec->value; }

int16_t PKT_$NEXT_ID(void) { return 0x0765; }

void PKT_$SEND_INTERNET(uint32_t routing_key, uint32_t dest_node,
                        uint16_t dest_sock, int32_t src_node_or,
                        uint32_t src_node, uint16_t src_sock,
                        void *pkt_info, uint16_t request_id,
                        void *template, uint16_t template_len,
                        void *data, int16_t data_len,
                        uint16_t *retry_hint, uint16_t *timeout_out,
                        status_$t *status_ret)
{
    (void)routing_key; (void)src_node_or; (void)src_node; (void)src_sock;
    (void)request_id;

    snd_calls++;
    snd_dest_node    = dest_node;
    snd_dest_sock    = dest_sock;
    snd_template     = template;
    snd_template_len = template_len;
    snd_data         = data;
    snd_data_len     = data_len;
    memcpy(snd_pkt_info, pkt_info, sizeof(snd_pkt_info));

    *retry_hint  = 5;
    *timeout_out = mock_send_timeout;
    *status_ret  = mock_send_status;
}

void PKT_$DUMP_DATA(uint32_t *buffers, int16_t len)
{
    (void)len;
    dump_calls++;
    dump_last_pages = buffers;
}

int16_t EC_$WAIT(ec_$wait_ecs_t ecs, ec_$wait_vals_t vals)
{
    (void)ecs; (void)vals;
    if (wait_calls < wait_script_len) {
        return wait_script[wait_calls++];
    }
    wait_calls++;
    return 1;
}

void APP_$RECEIVE(uint16_t sock_num, void *result, status_$t *status_ret)
{
    app_$receive_rec_t *rec = (app_$receive_rec_t *)result;

    (void)sock_num;
    recv_calls++;
    memset(rec, 0, sizeof(*rec));

    va_arena.reply_hdr.f_00         = 0;
    va_arena.reply_hdr.template_len = 0x20;
    va_arena.reply_hdr.data_len     = 0x40;
    va_arena.reply_hdr.reply_id     = mock_reply_id;

    rec->reply         = ARCH_PTR_TO_VA(&va_arena.reply_hdr);
    rec->data          = ARCH_PTR_TO_VA(va_arena.payload);
    rec->data_pages[0] = mock_bulk_handle;
    rec->hdr_f06       = mock_route_port;      /* +0x18 */
    rec->hdr_f12       = 0xBADBAD00u;          /* +0x1C - must NOT be used */

    *status_ret = mock_recv_status;
}

void NETBUF_$RTN_HDR(uint32_t *va_ptr) { rtn_hdr_calls++; rtn_hdr_last = *va_ptr; }

void RIP_$UPDATE_INT(uint32_t network, rip_$xns_addr_t *source,
                     uint16_t hop_count, uint16_t port_index,
                     boolean flags, status_$t *status_ret)
{
    (void)hop_count; (void)port_index;
    if (update_calls < 2) {
        update_network[update_calls] = network;
        update_source[update_calls]  = source;
        update_flags[update_calls]   = flags;
    }
    update_calls++;
    *status_ret = status_$ok;
}

/* ============================================================================
 * Fixtures
 * ============================================================================ */

static void reset(void)
{
    int i;

    memset(&RIP_$DATA, 0, sizeof(RIP_$DATA));
    memset(sock_table_base, 0, sizeof(sock_table_base));
    memset(&va_arena, 0, sizeof(va_arena));

    for (i = 0; i < 30; i++) {
        RIP_$DATA.bcast_control[i] = (uint8_t)(0x80 + i);
    }

    NETWORK_$DISKLESS = (int8_t)0xFF;       /* diskless */
    NETWORK_$MOTHER_NODE = 0xAABBCCDDu;
    NODE_$ME = 0x11223344u;
    TIME_$CLOCKH = 500;
    ROUTE_$PORT = 0;
    RIP_$INIT_REQUEST = 0;

    mock_sock_ec.value = 3;
    mock_bad_ec.value  = 99;
    SOCK_$EVENT_COUNTERS[mock_alloc_sock - 1] = &mock_sock_ec;
    SOCK_$EVENT_COUNTERS[mock_alloc_sock]     = &mock_bad_ec;

    excl_init_calls = 0;
    mock_alloc_result = -1;
    mock_send_status = status_$ok;
    mock_send_timeout = 6;
    snd_calls = 0;
    wait_script_len = 0; wait_calls = 0;
    mock_reply_id = 0x0765;
    mock_route_port = 0x0C0FFEE0u;
    mock_bulk_handle = 0;
    mock_recv_status = status_$ok;
    recv_calls = 0;
    rtn_hdr_calls = 0; rtn_hdr_last = 0;
    dump_calls = 0; dump_last_pages = NULL;
    close_calls = 0; closed_sock = 0xFFFF;
    update_calls = 0;
}

static void script_one_good_reply(void)
{
    wait_script[0] = 0;
    wait_script_len = 1;
}

/* ============================================================================
 * Tests
 * ============================================================================ */

TEST(the_three_locks_are_always_initialised)
{
    reset();
    NETWORK_$DISKLESS = 0;
    RIP_$INIT();

    ASSERT_EQ(3, excl_init_calls);
    ASSERT_EQ((unsigned long)(size_t)&RIP_$DATA.exclusion,
              (unsigned long)(size_t)excl_init_args[0]);
    ASSERT_EQ((unsigned long)(size_t)&RIP_$DATA.route_service_mutex,
              (unsigned long)(size_t)excl_init_args[1]);
    ASSERT_EQ((unsigned long)(size_t)&RIP_$DATA.xns_error_mutex,
              (unsigned long)(size_t)excl_init_args[2]);
}

TEST(a_non_diskless_node_stops_after_the_locks)
{
    reset();
    NETWORK_$DISKLESS = 0;
    RIP_$INIT();

    ASSERT_EQ(0, snd_calls);
    ASSERT_EQ(0, close_calls);
}

TEST(socket_allocation_failure_stops_before_the_send)
{
    reset();
    mock_alloc_result = 0;              /* non-negative = failure */
    RIP_$INIT();

    ASSERT_EQ(0, snd_calls);
    ASSERT_EQ(0, close_calls);          /* the exit is before the socket */
}

TEST(the_packet_info_template_is_copied_with_bit_7_of_byte_1_cleared)
{
    reset();
    script_one_good_reply();
    RIP_$INIT();

    ASSERT_EQ(0x80, snd_pkt_info[0]);
    ASSERT_EQ(0x01, snd_pkt_info[1]);   /* 0x81 with bit 7 cleared */
    ASSERT_EQ(0x82, snd_pkt_info[2]);
    ASSERT_EQ(0x9D, snd_pkt_info[29]);
}

TEST(the_template_and_data_arguments_are_real_cells)
{
    reset();
    script_one_good_reply();
    RIP_$INIT();

    ASSERT_EQ(1, snd_calls);
    ASSERT_EQ((unsigned long)(size_t)&RIP_$INIT_REQUEST,
              (unsigned long)(size_t)snd_template);
    ASSERT_EQ(2, snd_template_len);
    ASSERT_EQ(0, snd_data_len);
    /* the data pointer is a real cell, never NULL */
    ASSERT_EQ(1, snd_data != NULL);
    ASSERT_EQ(0xAABBCCDDu, snd_dest_node);
    ASSERT_EQ(1, snd_dest_sock);
}

TEST(a_send_failure_still_closes_the_socket)
{
    reset();
    mock_send_status = 0x00110004;
    RIP_$INIT();

    ASSERT_EQ(1, close_calls);
    ASSERT_EQ(7, closed_sock);
    ASSERT_EQ(0, update_calls);
}

TEST(a_timeout_before_any_reply_closes_the_socket)
{
    reset();
    wait_script[0] = 1;                 /* the deadline fires first */
    wait_script_len = 1;
    RIP_$INIT();

    ASSERT_EQ(0, recv_calls);
    ASSERT_EQ(1, close_calls);
    ASSERT_EQ(0, update_calls);
}

TEST(the_route_port_comes_from_record_offset_0x18)
{
    reset();
    script_one_good_reply();
    RIP_$INIT();

    ASSERT_EQ(0x0C0FFEE0u, ROUTE_$PORT);
    ASSERT_EQ(0x0C0FFEE0u, RIP_$DATA.route_port);
}

TEST(netbuf_rtn_hdr_gets_the_page_aligned_data_va)
{
    reset();
    script_one_good_reply();
    RIP_$INIT();

    ASSERT_EQ(1, rtn_hdr_calls);
    ASSERT_EQ(ARCH_PTR_TO_VA(va_arena.payload) & 0xFFFFFC00u, rtn_hdr_last);
}

TEST(the_page_vector_handed_to_dump_data_is_record_offset_8)
{
    reset();
    mock_bulk_handle = 0x1234;
    script_one_good_reply();
    RIP_$INIT();

    ASSERT_EQ(1, dump_calls);
    ASSERT_EQ(0x1234u, dump_last_pages[0]);
}

TEST(no_pages_means_no_dump)
{
    reset();
    mock_bulk_handle = 0;
    script_one_good_reply();
    RIP_$INIT();

    ASSERT_EQ(0, dump_calls);
}

TEST(a_mismatched_reply_id_keeps_waiting)
{
    reset();
    mock_reply_id = 0x0001;             /* not PKT_$NEXT_ID's 0x0765 */
    wait_script[0] = 0;
    wait_script[1] = 1;                 /* then the deadline fires */
    wait_script_len = 2;
    RIP_$INIT();

    ASSERT_EQ(1, recv_calls);
    ASSERT_EQ(0, update_calls);
    ASSERT_EQ(1, close_calls);
}

TEST(both_update_calls_share_the_source_and_differ_in_the_boolean)
{
    reset();
    script_one_good_reply();
    RIP_$INIT();

    ASSERT_EQ(2, update_calls);
    ASSERT_EQ(0x0C0FFEE0u, update_network[0]);
    ASSERT_EQ(0x0C0FFEE0u, update_network[1]);
    ASSERT_EQ((unsigned long)(size_t)&RIP_$DATA,
              (unsigned long)(size_t)update_source[0]);
    ASSERT_EQ((unsigned long)(size_t)&RIP_$DATA,
              (unsigned long)(size_t)update_source[1]);
    ASSERT_EQ(0x00, (uint8_t)update_flags[0]);
    ASSERT_EQ(0xFF, (uint8_t)update_flags[1]);
    ASSERT_EQ(1, close_calls);
}

/* ============================================================================
 * main
 * ============================================================================ */

int main(void)
{
    ARCH_HOST_VA_BASE = (uintptr_t)&va_arena - 0x800;

    printf("RIP_$INIT tests\n");

    RUN_TEST(the_three_locks_are_always_initialised);
    RUN_TEST(a_non_diskless_node_stops_after_the_locks);
    RUN_TEST(socket_allocation_failure_stops_before_the_send);
    RUN_TEST(the_packet_info_template_is_copied_with_bit_7_of_byte_1_cleared);
    RUN_TEST(the_template_and_data_arguments_are_real_cells);
    RUN_TEST(a_send_failure_still_closes_the_socket);
    RUN_TEST(a_timeout_before_any_reply_closes_the_socket);
    RUN_TEST(the_route_port_comes_from_record_offset_0x18);
    RUN_TEST(netbuf_rtn_hdr_gets_the_page_aligned_data_va);
    RUN_TEST(the_page_vector_handed_to_dump_data_is_record_offset_8);
    RUN_TEST(no_pages_means_no_dump);
    RUN_TEST(a_mismatched_reply_id_keeps_waiting);
    RUN_TEST(both_update_calls_share_the_source_and_differ_in_the_boolean);

    printf("\n%d tests, %d failures\n", tests_run, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
