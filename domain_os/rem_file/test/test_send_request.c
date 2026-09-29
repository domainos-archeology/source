/*
 * rem_file/test/test_send_request.c - unit tests for REM_FILE_$SEND_REQUEST
 * (0x00E60FD8)
 *
 * The real rem_file/send_request.c is #included below and every routine it
 * calls is mocked here.  What is checked is the set of stores the 2026-09-07
 * re-emission recovered (bead source-ldrp):
 *
 *   - the two early exits, 0x000F0001 for a type-9 process (0x00E61002) and
 *     0x000F0004 for a non-local address without the network capability
 *     (0x00E6102A)
 *   - request->msg_type = 1 (0x00E6105C)
 *   - the socket-table index, SOCK_$EVENT_COUNTERS[sock - 1] (0x00E610B8)
 *   - the retry-exhausted and send-failure 0x000F0004 (0x00E611BC)
 *   - the quit exit: 0x00120010 with bit 31 set (0x00E614A4/AA)
 *   - the no-answer exit 0x00110007 (0x00E6150C)
 *   - the reply-opcode check: status from response+4 when
 *     response[3] == request[3] + 1, else 0x000F0003 (0x00E6146C/78)
 *   - *packet_id at every exit that reaches 0x00E61512
 *   - the bulk_len rules: min(reply, response_max - received) with bulk_max
 *     0, min(reply, bulk_max) otherwise, and 0 written back when bulk_max is
 *     0 (0x00E61338 / 0x00E61354 / 0x00E613C6)
 */

#include <stdio.h>
#include <string.h>

#include "rem_file/rem_file_internal.h"
#include "arch/arch.h"

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

#include "../send_request.c"

/* ============================================================================
 * Globals the unit reads
 * ============================================================================ */

#include "proc1/proc1.h"
MODULE_DATA_DEFINE(proc1_$data_t, PROC1_$DATA, 0x00E254E8);
#include "fim/fim.h"
MODULE_DATA_DEFINE(fim_$wired_data_t, FIM_$WIRED_DATA, 0x00E21FE6);
uint16_t  PROC1_$CURRENT;
uint16_t  PROC1_$AS_ID;
uint32_t  NETWORK_$ALLOWED_SERVICE;      /* NETWORK_$CAPABLE_FLAGS is bits 16..23 */
int8_t    NETWORK_$DISKLESS;
uint32_t  NETWORK_$MOTHER_NODE;
uint32_t  NODE_$ME;
uint32_t  TIME_$CLOCKH;
uint8_t   REM_FILE_$DATA[0x1E];
uint32_t  REM_FILE_$BUSY_RETRY_COUNT;
uint16_t  REM_FILE_$COMPLETION_TIME = 20;
MODULE_DATA_DEFINE(sock_$data_t, SOCK_$DATA, 0x00E27510);

/* ============================================================================
 * Mocks
 * ============================================================================ */

static ec_$eventcount_t mock_sock_ec;

/* Scripted EC_$WAIT results, consumed one per call. */
#define MAX_WAIT_SCRIPT 32
static int16_t  wait_script[MAX_WAIT_SCRIPT];
static int      wait_script_len;
static int      wait_calls;

/* Scripted APP_$RECEIVE outcomes, consumed one per call. */
typedef struct {
    status_$t status;
    uint16_t  template_len;     /* reply hdr +0x02 */
    uint16_t  data_len;         /* reply hdr +0x04 */
    int16_t   reply_id;         /* reply hdr +0x06 */
    uint32_t  bulk_handle;      /* rcv.data_pages[0] */
} recv_step_t;

#define MAX_RECV_SCRIPT 8
static recv_step_t recv_script[MAX_RECV_SCRIPT];
static int      recv_script_len;
static int      recv_calls;

/*
 * Everything the unit reaches through a 32-bit VA lives in one arena, and
 * ARCH_HOST_VA_BASE is set just below it in main(), so ARCH_PTR_TO_VA fits in
 * 32 bits on a 64-bit host.
 */
static struct {
    app_$reply_hdr_t reply_hdr;
    union {
        rem_file_$response_t rec;
        uint8_t              raw[0x200];
    } reply_payload;
    uint8_t  bulk_payload[0x400];
} va_arena;

#define mock_reply_hdr      va_arena.reply_hdr
#define mock_reply_payload  va_arena.reply_payload.raw
#define mock_reply_rec      va_arena.reply_payload.rec
#define mock_bulk_payload   va_arena.bulk_payload

static int16_t  mock_next_id = 0x4321;
static int8_t   mock_alloc_result = -1;   /* 0xFF = success */
static int8_t   mock_likely_result = -1;  /* negative = "probably answering" */
static status_$t mock_send_status;
static uint16_t mock_send_timeout;

static int      mock_close_calls;
static uint16_t mock_closed_sock;
static int      mock_note_visible_calls;
static boolean  mock_note_visible_last;
static int      mock_crash_calls;
static status_$t mock_crash_status;
static int      mock_dump_calls;
static int      mock_rtn_dat_calls;
static uint32_t mock_last_copy_len;
static uint8_t *mock_last_copy_dst;

int8_t SOCK_$ALLOCATE(uint16_t *sock_ret, uint32_t proto_bufpages,
                      uint32_t max_queue)
{
    (void)proto_bufpages; (void)max_queue;
    *sock_ret = 5;
    return mock_alloc_result;
}

void SOCK_$CLOSE(uint16_t sock_num)
{
    mock_close_calls++;
    mock_closed_sock = sock_num;
}

int16_t PKT_$NEXT_ID(void) { return mock_next_id; }

void PKT_$SEND_INTERNET(uint32_t routing_key, uint32_t dest_node,
                        uint16_t dest_sock, int32_t src_node_or,
                        uint32_t src_node, uint16_t src_sock,
                        void *pkt_info, uint16_t request_id,
                        void *template, uint16_t template_len,
                        void *data, int16_t data_len,
                        uint16_t *retry_hint, uint16_t *timeout_out,
                        status_$t *status_ret)
{
    (void)routing_key; (void)dest_node; (void)dest_sock; (void)src_node_or;
    (void)src_node; (void)src_sock; (void)pkt_info; (void)request_id;
    (void)template; (void)template_len; (void)data; (void)data_len;

    *retry_hint  = 5;
    *timeout_out = mock_send_timeout;
    *status_ret  = mock_send_status;
}

void PKT_$NOTE_VISIBLE(uint32_t node_id, boolean is_visible)
{
    (void)node_id;
    mock_note_visible_calls++;
    mock_note_visible_last = is_visible;
}

void PKT_$DUMP_DATA(uint32_t *buffers, int16_t len)
{
    (void)buffers; (void)len;
    mock_dump_calls++;
}

boolean PKT_$LIKELY_TO_ANSWER(void *addr_info, status_$t *status_ret)
{
    (void)addr_info; (void)status_ret;
    return mock_likely_result;
}

int16_t EC_$WAIT(ec_$wait_ecs_t ecs, ec_$wait_vals_t vals)
{
    (void)ecs; (void)vals;
    if (wait_calls < wait_script_len) {
        return wait_script[wait_calls++];
    }
    wait_calls++;
    return 1;   /* fall back to "the deadline fired" so nothing spins */
}

void APP_$RECEIVE(uint16_t sock_num, void *result, status_$t *status_ret)
{
    app_$receive_rec_t *rec = (app_$receive_rec_t *)result;
    const recv_step_t *step;

    (void)sock_num;
    memset(rec, 0, sizeof(*rec));

    if (recv_calls >= recv_script_len) {
        recv_calls++;
        *status_ret = status_$network_buffer_queue_is_empty;
        return;
    }
    step = &recv_script[recv_calls++];

    mock_reply_hdr.magic        = 0;
    mock_reply_hdr.template_len = step->template_len;
    mock_reply_hdr.data_len     = step->data_len;
    mock_reply_hdr.request_id     = step->reply_id;

    rec->reply = ARCH_PTR_TO_VA(&mock_reply_hdr);
    rec->data  = ARCH_PTR_TO_VA(mock_reply_payload);
    rec->data_pages[0] = step->bulk_handle;

    *status_ret = step->status;
}

void NETBUF_$RTN_HDR(uint32_t *va_ptr) { (void)va_ptr; }
void NETBUF_$RTN_DAT(uint32_t addr) { (void)addr; mock_rtn_dat_calls++; }
uint32_t NETBUF_$RTNVA(uint32_t *va_ptr) { return *va_ptr; }

void NETBUF_$GETVA(uint32_t ppn_shifted, uint32_t *va_out, status_$t *status)
{
    (void)ppn_shifted;
    *va_out = ARCH_PTR_TO_VA(mock_bulk_payload);
    *status = status_$ok;
}

void OS_$DATA_COPY(const void *src, void *dst, uint32_t len)
{
    mock_last_copy_len = len;
    mock_last_copy_dst = (uint8_t *)dst;
    if (len != 0) {
        memmove(dst, src, len);
    }
}

status_$t FIM_$CLEANUP(void *handler)
{
    (void)handler;
    return status_$cleanup_handler_set;
}

void FIM_$RLS_CLEANUP(void *cleanup_data) { (void)cleanup_data; }
void FIM_$SIGNAL(status_$t status) { (void)status; }

void CRASH_SYSTEM(const status_$t *status_p)
{
    mock_crash_calls++;
    mock_crash_status = *status_p;
}

int32_t EC_$READ(ec_$eventcount_t *ec) { return ec->value; }

/* ============================================================================
 * Fixtures
 * ============================================================================ */

static uint32_t addr_info[2];
static uint8_t  request_buf[0x40];
static uint8_t  response_buf[REM_FILE_RESPONSE_BUF_SIZE];
static uint8_t  bulk_buf[0x400];
static uint16_t received_len;
static int16_t  bulk_len;
static uint16_t packet_id;
static status_$t st;

static void reset(void)
{
    memset(PROC1_$DATA.type, 0, sizeof(PROC1_$DATA.type));
    PROC1_$CURRENT = 3;
    PROC1_$AS_ID   = 2;
    NETWORK_$ALLOWED_SERVICE = 0x00010000u;     /* capability bit 0 set */
    NETWORK_$DISKLESS = 0;
    NETWORK_$MOTHER_NODE = 0;
    NODE_$ME = 0x11112222u;
    TIME_$CLOCKH = 1000;
    REM_FILE_$BUSY_RETRY_COUNT = 0;

    memset(FIM_$WIRED_DATA.quit_value, 0, sizeof(FIM_$WIRED_DATA.quit_value));
    memset(FIM_$WIRED_DATA.quit_ec, 0, sizeof(FIM_$WIRED_DATA.quit_ec));

    mock_sock_ec.value = 7;
    memset(&SOCK_$DATA, 0, sizeof(SOCK_$DATA));
    /* socket 5's descriptor pointer is SOCK_$DATA.socket_ptr[5] */
    SOCK_$DATA.socket_ptr[5] = (sock_$sock_t *)&mock_sock_ec;

    wait_script_len = 0; wait_calls = 0;
    recv_script_len = 0; recv_calls = 0;

    mock_next_id = 0x4321;
    mock_alloc_result = -1;
    mock_likely_result = -1;
    mock_send_status = status_$ok;
    mock_send_timeout = 4;
    mock_close_calls = 0;
    mock_closed_sock = 0xFFFF;
    mock_note_visible_calls = 0;
    mock_note_visible_last = 0;
    mock_crash_calls = 0;
    mock_crash_status = 0;
    mock_dump_calls = 0;
    mock_rtn_dat_calls = 0;
    mock_last_copy_len = 0xFFFFFFFFu;
    mock_last_copy_dst = NULL;

    memset(request_buf, 0, sizeof(request_buf));
    memset(response_buf, 0, sizeof(response_buf));
    memset(bulk_buf, 0, sizeof(bulk_buf));
    memset(mock_reply_payload, 0xC3, sizeof(mock_reply_payload));
    memset(mock_bulk_payload, 0xB7, sizeof(mock_bulk_payload));

    /* request opcode 0x10; a good reply carries 0x11 */
    request_buf[2] = REM_FILE_REQ_MAGIC;
    request_buf[3] = REM_FILE_OP_NEIGHBORS;

    addr_info[0] = 0x0A0A0A0Au;
    addr_info[1] = 0x33334444u;

    received_len = 0xEEEE;
    bulk_len = 0x7777;
    packet_id = 0xEEEE;
    st = 0x7FFFFFFF;
}

static void call(uint16_t response_max, void *bulk_data, int16_t bulk_max)
{
    REM_FILE_$SEND_REQUEST(addr_info, request_buf, 0x18,
                           NULL, 0,
                           response_buf, response_max,
                           &received_len, bulk_data, bulk_max,
                           &bulk_len, &packet_id, &st);
}

/* A single successful exchange: the wait reports the socket, the receive
 * hands back a matching reply. */
static void script_one_good_reply(uint16_t template_len, uint16_t data_len,
                                  uint32_t bulk_handle)
{
    wait_script[0] = 0;
    wait_script_len = 1;

    recv_script[0].status       = status_$ok;
    recv_script[0].template_len = template_len;
    recv_script[0].data_len     = data_len;
    recv_script[0].reply_id     = mock_next_id;
    recv_script[0].bulk_handle  = bulk_handle;
    recv_script_len = 1;

    /* The reply the copy brings across.  Named fields only - the host is
     * little-endian, so raw wire bytes would not read back as a longword. */
    mock_reply_rec.pkt_flag = 0;
    mock_reply_rec.magic    = REM_FILE_REQ_MAGIC;
    mock_reply_rec.opcode   = (uint8_t)(REM_FILE_OP_NEIGHBORS + 1);
    mock_reply_rec.status   = 0x00002222;
}

/* ============================================================================
 * Early exits
 * ============================================================================ */

TEST(type_9_process_is_refused)
{
    reset();
    PROC1_$DATA.type[PROC1_$CURRENT] = 9;
    call(0x40, NULL, 0);

    ASSERT_EQ(0x000F0001, st);          /* file_$object_not_found, 0x00E61002 */
    ASSERT_EQ(0, mock_close_calls);     /* the exit is before the socket */
    ASSERT_EQ(0xEEEE, packet_id);       /* and before *packet_id */
}

TEST(not_capable_and_not_local_is_refused)
{
    reset();
    NETWORK_$ALLOWED_SERVICE = 0;       /* capability bit 0 clear */
    addr_info[1] = NODE_$ME + 1;
    call(0x40, NULL, 0);

    ASSERT_EQ(0x000F0004, st);          /* 0x00E6102A */
    ASSERT_EQ(0, mock_close_calls);
}

TEST(not_capable_but_local_proceeds)
{
    reset();
    NETWORK_$ALLOWED_SERVICE = 0;
    addr_info[1] = NODE_$ME;
    script_one_good_reply(0x20, 0, 0);
    call(0x40, NULL, 0);

    ASSERT_EQ(0x00002222, st);          /* the server's own status */
    ASSERT_EQ(1, mock_close_calls);
}

/* ============================================================================
 * Socket allocation
 * ============================================================================ */

TEST(socket_allocation_failure_crashes_with_0x110005)
{
    reset();
    mock_alloc_result = 0;              /* non-negative = failure */
    script_one_good_reply(0x20, 0, 0);
    call(0x40, NULL, 0);

    ASSERT_EQ(1, mock_crash_calls);
    ASSERT_EQ(0x00110005, mock_crash_status);   /* the cell at 0xE61530 */
}

TEST(split_request_with_extra_data_crashes_with_0x110001)
{
    reset();
    script_one_good_reply(0x20, 0, 0);
    /* request_len > 0x200 and extra_len != 0 */
    {
        uint16_t nonzero = 1;
        REM_FILE_$SEND_REQUEST(addr_info, request_buf, 0x201,
                               &nonzero, 1,
                               response_buf, 0x40,
                               &received_len, NULL, 0,
                               &bulk_len, &packet_id, &st);
    }
    ASSERT_EQ(1, mock_crash_calls);
    ASSERT_EQ(0x00110001, mock_crash_status);   /* the cell at 0xE61534 */
}

TEST(socket_event_counter_is_indexed_from_one)
{
    reset();
    /* Put a decoy at [5]; the unit must use [5 - 1]. */
    SOCK_$DATA.socket_ptr[6] = NULL;
    script_one_good_reply(0x20, 0, 0);
    call(0x40, NULL, 0);

    /* If the unit had used [sock] it would have dereferenced NULL. */
    ASSERT_EQ(0x00002222, st);
    ASSERT_EQ(5, mock_closed_sock);
}

/* ============================================================================
 * The request header and the packet id
 * ============================================================================ */

TEST(msg_type_is_stamped_into_the_caller_request)
{
    reset();
    script_one_good_reply(0x20, 0, 0);
    call(0x40, NULL, 0);

    ASSERT_EQ(1, ((rem_file_request_hdr_t *)request_buf)->msg_type);
    ASSERT_EQ(REM_FILE_REQ_MAGIC, request_buf[2]);      /* untouched */
    ASSERT_EQ(REM_FILE_OP_NEIGHBORS, request_buf[3]);
}

TEST(packet_id_is_written_at_every_socket_exit)
{
    reset();
    script_one_good_reply(0x20, 0, 0);
    call(0x40, NULL, 0);
    ASSERT_EQ(0x4321, packet_id);
}

/* ============================================================================
 * Status outcomes
 * ============================================================================ */

TEST(send_failure_reports_comms_problem)
{
    reset();
    mock_send_status = 0x00110004;      /* transmit failed */
    call(0x40, NULL, 0);

    ASSERT_EQ(0x000F0004, st);          /* 0x00E611BC */
    ASSERT_EQ(1, mock_close_calls);
    ASSERT_EQ(0x4321, packet_id);
}

TEST(retry_exhausted_reports_comms_problem_and_notes_invisible)
{
    reset();
    /* Every wait reports the timer, and the quit value never moves, so the
     * retry counter climbs by 12 a time until it passes 0x3C. */
    mock_likely_result = -1;            /* keep probing "yes" -> state 3 */
    call(0x40, NULL, 0);

    ASSERT_EQ(0x000F0004, st);
    ASSERT_EQ(1, mock_close_calls);
    ASSERT_EQ(0x4321, packet_id);
    /* the last visibility note is `false` (0x00E61140) */
    ASSERT_EQ(0, (uint8_t)mock_note_visible_last);
}

TEST(quit_sets_0x120010_with_bit_31)
{
    reset();
    wait_script[0] = 1;                 /* the timer fires */
    wait_script_len = 1;
    /* make the quit eventcount disagree with the snapshot */
    FIM_$WIRED_DATA.quit_value[PROC1_$AS_ID] = 0;
    FIM_$WIRED_DATA.quit_ec[PROC1_$AS_ID].value = 9;
    call(0x40, NULL, 0);

    ASSERT_EQ(0x80120010u, (uint32_t)st);
    ASSERT_EQ(9, FIM_$WIRED_DATA.quit_value[PROC1_$AS_ID]);
    ASSERT_EQ(1, mock_close_calls);
}

TEST(no_answer_reports_0x110007)
{
    reset();
    /* two timer expiries: the first moves state 0 -> 1, the second probes */
    wait_script[0] = 1;
    wait_script[1] = 1;
    wait_script_len = 2;
    mock_likely_result = 0;             /* non-negative = "not answering" */
    call(0x40, NULL, 0);

    ASSERT_EQ(0x00110007, st);          /* 0x00E6150C */
    ASSERT_EQ(1, mock_close_calls);
    ASSERT_EQ(0x4321, packet_id);
}

TEST(opcode_mismatch_reports_0xF0003)
{
    reset();
    script_one_good_reply(0x20, 0, 0);
    mock_reply_rec.opcode = REM_FILE_OP_NEIGHBORS + 2;  /* not opcode + 1 */
    call(0x40, NULL, 0);

    ASSERT_EQ(0x000F0003, st);          /* 0x00E61478 */
}

TEST(matching_opcode_takes_the_status_from_response_plus_4)
{
    reset();
    script_one_good_reply(0x20, 0, 0);
    mock_reply_rec.status = 0x000F0010;
    call(0x40, NULL, 0);

    ASSERT_EQ(0x000F0010, st);          /* 0x00E6146C */
}

TEST(busy_reply_retries_and_bumps_the_counter)
{
    reset();
    /* first exchange: a busy reply (first word 0xFFFF); then let the retry
     * time out so the call finishes. */
    wait_script[0] = 0;
    wait_script[1] = 1;
    wait_script[2] = 1;
    wait_script_len = 3;

    recv_script[0].status       = status_$ok;
    recv_script[0].template_len = 0x20;
    recv_script[0].data_len     = 0;
    recv_script[0].reply_id     = mock_next_id;
    recv_script[0].bulk_handle  = 0;
    recv_script_len = 1;

    mock_reply_rec.pkt_flag = 0xFFFF;           /* the busy marker */
    mock_likely_result = 0;
    call(0x40, NULL, 0);

    ASSERT_EQ(1, REM_FILE_$BUSY_RETRY_COUNT);   /* 0x00E6141C */
    /* The reply moved the state to CONFIRMED (0x00E613F8), which is what the
     * timeout arm at 0x00E614DC falls through on - so no PKT_$LIKELY_TO_ANSWER
     * probe ever happens and the call ends on the retry budget. */
    ASSERT_EQ(0x000F0004, st);
    ASSERT_EQ(2, mock_note_visible_calls);      /* true, then false */
    ASSERT_EQ(0, (uint8_t)mock_note_visible_last);
}

/* ============================================================================
 * received_len and the bulk rules
 * ============================================================================ */

TEST(received_len_is_clipped_to_response_max)
{
    reset();
    script_one_good_reply(0x100, 0, 0);         /* reply says 0x100 bytes */
    call(0x40, NULL, 0);                        /* caller allows 0x40 */

    ASSERT_EQ(0x40, received_len);              /* 0x00E6128C */
    ASSERT_EQ(0x40, mock_last_copy_len);
}

TEST(received_len_keeps_the_reply_length_when_it_fits)
{
    reset();
    script_one_good_reply(0x20, 0, 0);
    call(0x40, NULL, 0);

    ASSERT_EQ(0x20, received_len);
}

TEST(bulk_len_is_the_reply_field_when_no_pages_arrive)
{
    reset();
    script_one_good_reply(0x20, 0x30, 0);       /* data_len 0x30, handle 0 */
    call(0x40, NULL, 0);

    /* Nothing clamps it: the bulk block is skipped when data_pages[0] == 0
     * (0x00E612EA), so *bulk_len keeps the reply's own field. */
    ASSERT_EQ(0x30, bulk_len);
}

TEST(bulk_len_is_clipped_to_bulk_max_when_a_buffer_is_given)
{
    reset();
    script_one_good_reply(0x20, 0x300, 1);      /* a page handle arrives */
    call(0x40, bulk_buf, 0x80);

    ASSERT_EQ(0x80, bulk_len);                  /* 0x00E61354 */
    ASSERT_EQ(0x80, mock_last_copy_len);
    ASSERT_EQ((unsigned long)(size_t)bulk_buf, (unsigned long)(size_t)mock_last_copy_dst);
}

TEST(bulk_len_is_zeroed_when_the_payload_is_appended_to_the_reply)
{
    reset();
    script_one_good_reply(0x20, 0x10, 1);
    call(0x40, NULL, 0);                        /* bulk_max == 0 */

    /* the payload is copied to response + received_len, clipped to
     * response_max - received_len, and *bulk_len is then cleared */
    ASSERT_EQ(0, bulk_len);                     /* 0x00E613C6 */
    ASSERT_EQ(0x10, mock_last_copy_len);
    ASSERT_EQ((unsigned long)(size_t)(response_buf + 0x20),
              (unsigned long)(size_t)mock_last_copy_dst);
}

TEST(appended_payload_is_clipped_to_the_room_left_in_the_reply_buffer)
{
    reset();
    script_one_good_reply(0x38, 0x100, 1);      /* reply fills 0x38 of 0x40 */
    call(0x40, NULL, 0);

    /* room left is 0x40 - 0x38 = 8 */
    ASSERT_EQ(8, mock_last_copy_len);           /* 0x00E61338 */
    ASSERT_EQ(0, bulk_len);
}

TEST(oversized_bulk_reply_is_dumped_and_the_wait_resumes)
{
    reset();
    wait_script[0] = 0;
    wait_script[1] = 1;
    wait_script[2] = 1;
    wait_script_len = 3;

    recv_script[0].status       = status_$ok;
    recv_script[0].template_len = 0x20;
    recv_script[0].data_len     = 0x401;        /* > 0x400 */
    recv_script[0].reply_id     = mock_next_id;
    recv_script[0].bulk_handle  = 1;
    recv_script_len = 1;

    mock_likely_result = 0;
    call(0x40, NULL, 0);

    ASSERT_EQ(1, mock_dump_calls);              /* 0x00E612D8 */
    ASSERT_EQ(0x401, bulk_len);                 /* stored before the check */
    ASSERT_EQ(0x00110007, st);                  /* the retry then times out */
}

TEST(mismatched_reply_id_keeps_waiting)
{
    reset();
    wait_script[0] = 0;
    wait_script[1] = 1;
    wait_script[2] = 1;
    wait_script_len = 3;

    recv_script[0].status       = status_$ok;
    recv_script[0].template_len = 0x20;
    recv_script[0].data_len     = 0;
    recv_script[0].reply_id     = (int16_t)(mock_next_id + 1);
    recv_script[0].bulk_handle  = 0;
    recv_script_len = 1;

    mock_likely_result = 0;
    call(0x40, NULL, 0);

    /* the stale reply never became the answer */
    ASSERT_EQ(0x00110007, st);
    ASSERT_EQ(0, mock_note_visible_calls);
}

/* ============================================================================
 * main
 * ============================================================================ */

int main(void)
{
    /* the netbuf pointers the unit follows are 32-bit VAs */
    ARCH_HOST_VA_BASE = (uintptr_t)&va_arena - 0x10;

    printf("REM_FILE_$SEND_REQUEST tests\n");

    RUN_TEST(type_9_process_is_refused);
    RUN_TEST(not_capable_and_not_local_is_refused);
    RUN_TEST(not_capable_but_local_proceeds);
    RUN_TEST(socket_allocation_failure_crashes_with_0x110005);
    RUN_TEST(split_request_with_extra_data_crashes_with_0x110001);
    RUN_TEST(socket_event_counter_is_indexed_from_one);
    RUN_TEST(msg_type_is_stamped_into_the_caller_request);
    RUN_TEST(packet_id_is_written_at_every_socket_exit);
    RUN_TEST(send_failure_reports_comms_problem);
    RUN_TEST(retry_exhausted_reports_comms_problem_and_notes_invisible);
    RUN_TEST(quit_sets_0x120010_with_bit_31);
    RUN_TEST(no_answer_reports_0x110007);
    RUN_TEST(opcode_mismatch_reports_0xF0003);
    RUN_TEST(matching_opcode_takes_the_status_from_response_plus_4);
    RUN_TEST(busy_reply_retries_and_bumps_the_counter);
    RUN_TEST(received_len_is_clipped_to_response_max);
    RUN_TEST(received_len_keeps_the_reply_length_when_it_fits);
    RUN_TEST(bulk_len_is_the_reply_field_when_no_pages_arrive);
    RUN_TEST(bulk_len_is_clipped_to_bulk_max_when_a_buffer_is_given);
    RUN_TEST(bulk_len_is_zeroed_when_the_payload_is_appended_to_the_reply);
    RUN_TEST(appended_payload_is_clipped_to_the_room_left_in_the_reply_buffer);
    RUN_TEST(oversized_bulk_reply_is_dumped_and_the_wait_resumes);
    RUN_TEST(mismatched_reply_id_keeps_waiting);

    printf("\n%d tests, %d failures\n", tests_run, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
