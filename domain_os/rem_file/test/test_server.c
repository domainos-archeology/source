/*
 * rem_file/test/test_server.c - unit tests for REM_FILE_$SERVER (0x00E63586)
 *
 * The real rem_file/server.c is #included below; every routine it calls out
 * to is mocked here.  The behaviours exercised are the ones the 2026-09-06
 * audit found missing or wrong:
 *
 *   - the receive parse (request length clamp, backlog histogram);
 *   - the version override at 0x00E63810;
 *   - the DIR_$SERVER (0x2A..0x5C) and ACL_$SERVER (0x64..0x77) delegation
 *     ranges;
 *   - the response path at 0x00E64144-0x00E64528: the response header, the
 *     per-opcode reply lengths, the 0x200 header/bulk split, the per-opcode
 *     bulk lengths, the 0x400 cap that raises 0x00110001, and that a reply
 *     is actually sent;
 *   - the status constants 0x00110006, 0x0011000F and 0x0011001C;
 *   - the NETLOG opcode-to-log-code map including the two folded ranges.
 */

#include <stdio.h>
#include <string.h>

#include "rem_file/rem_file_internal.h"
#include "file/file.h"
#include "network/network.h"
#include "name/name.h"      /* name_$data_t, NAME_$DATA */

/* ============================================================================
 * Test framework
 * ============================================================================ */

static int tests_run = 0;
static int tests_failed = 0;
static int current_failed = 0;

#define TEST(name)      static void test_##name(void)
#define RUN_TEST(name)  do {                                                  \
        printf("  %-44s ", #name);                                            \
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
 * Globals the code under test references
 * ============================================================================ */

uint32_t  NETWORK_$ALLOWED_SERVICE;   /* NETWORK_$CAPABLE_FLAGS is bits 16..23 */
int8_t    NETWORK_$DISKLESS;
int8_t    NETWORK_$REALLY_DISKLESS;
uint32_t  NETWORK_$MOTHER_NODE;
uint32_t  NETWORK_$FILE_BACKLOG[NETWORK_FILE_BACKLOG_BUCKETS];
uint32_t  network_file_backlog_tail[16];    /* buckets 1..8 plus slack */
uint8_t  *NETWORK_$SERVICE_INFO_PTR;
uint32_t  NODE_$ME;
uint32_t  TIME_$CLOCKH;
int8_t    NETLOG_$OK_TO_LOG;
int8_t    NETLOG_$OK_TO_LOG_SERVER;
int8_t    AUDIT_$ENABLED;
uint16_t  PROC1_$AS_ID;
uint16_t  PROC1_$CURRENT;
uid_t     UID_$NIL;
uid_t     SLINK_$UID;
name_$data_t NAME_$DATA;
uid_t     RGYC_$G_LOCKSMITH_UID;

ml_$exclusion_t REM_FILE_$SOCK_LOCK;
uint32_t        REM_FILE_$STALE_LINK_COUNT;
status_$t       REM_FILE_$COMMS_PROBLEM_STATUS = 0x000F0004;
uint32_t        REM_FILE_$NIL_CONST;
uint16_t        REM_FILE_$MAX_PROJ_LIST = 8;
uint16_t        REM_FILE_$MAX_NAME_LEN = 0x20;
uint8_t         REM_FILE_$SERVER_PKT_INFO[32];
char            REM_FILE_$DISKLESS_CRASH_MSG[] = "*** diskless partner node";

/* ============================================================================
 * Mock bookkeeping
 * ============================================================================ */

static status_$t mock_recv_status;
static uint8_t   mock_pkt_hdr[0x20];
static uint8_t   mock_req_src[0x400];
static rem_file_rcv_t mock_rcv_template;

static int       mock_send_calls;
static uint16_t  mock_send_tpl_len;
static int16_t   mock_send_data_len;
static void     *mock_send_data;
static uint16_t  mock_send_request_id;
static uint32_t  mock_send_dest_node;
static void     *mock_send_tpl;

static int       mock_dir_server_calls;
static int       mock_acl_server_calls;
static uint16_t  mock_dir_server_reply_len;
static uint16_t  mock_acl_server_reply_len;

static int       mock_excl_start;
static int       mock_excl_stop;
static int       mock_crash_calls;
static int       mock_rtn_hdr_calls;
static int       mock_dump_calls;
static int16_t   mock_dump_len;
static int       mock_dat_copy_calls;
static int16_t   mock_dat_copy_len;
static int       mock_getva_calls;
static int       mock_rtn_dat_calls;
static int       mock_get_dat_calls;
static uint32_t  mock_getva_result;

static int       mock_netlog_calls;
static uint16_t  mock_netlog_p5;
static uint16_t  mock_netlog_p6;

static int       mock_neighbors_calls;
static int       mock_truncate_calls;

static rem_file_server_resp_t sent_resp;
static void capture_response(const void *tpl)
{
    memcpy(&sent_resp, tpl, sizeof(sent_resp));
}

/* ============================================================================
 * Mocks
 * ============================================================================ */

void ML_$EXCLUSION_START(ml_$exclusion_t *l) { (void)l; mock_excl_start++; }
void ML_$EXCLUSION_STOP(ml_$exclusion_t *l)  { (void)l; mock_excl_stop++; }
void ML_$LOCK(int16_t id)   { (void)id; }
void ML_$UNLOCK(int16_t id) { (void)id; }

void APP_$RECEIVE(uint16_t sock, void *result, status_$t *status)
{
    (void)sock;
    memcpy(result, &mock_rcv_template, sizeof(mock_rcv_template));
    *status = mock_recv_status;
}

void OS_$DATA_COPY(const void *src, void *dst, uint32_t len)
{
    memcpy(dst, src, len);
}

void TIME_$ABS_CLOCK(clock_t *c) { memset(c, 0, 6); }

void NETBUF_$RTN_HDR(uint32_t *va) { (void)va; mock_rtn_hdr_calls++; }
void NETBUF_$GET_DAT(uint32_t *out) { (void)out; mock_get_dat_calls++; }
void NETBUF_$GETVA(uint32_t ppn, uint32_t *va_out, status_$t *status)
{
    (void)ppn;
    mock_getva_calls++;
    *va_out = mock_getva_result;
    *status = status_$ok;
}
uint32_t NETBUF_$RTNVA(uint32_t *va) { return *va; }
void NETBUF_$RTN_DAT(uint32_t addr) { (void)addr; mock_rtn_dat_calls++; }

void PKT_$DUMP_DATA(uint32_t *bufs, int16_t len)
{
    (void)bufs; mock_dump_calls++; mock_dump_len = len;
}
void PKT_$DAT_COPY(uint32_t *bufs, int16_t len, char *dst)
{
    (void)bufs; (void)dst; mock_dat_copy_calls++; mock_dat_copy_len = len;
}

void PKT_$SEND_INTERNET(uint32_t routing_key, uint32_t dest_node,
                        uint16_t dest_sock, int32_t src_override,
                        uint32_t src_node, uint16_t src_sock,
                        void *pkt_info, uint16_t request_id,
                        void *tpl, uint16_t tpl_len,
                        void *data, int16_t data_len,
                        uint16_t *len_out, uint16_t *extra, status_$t *status)
{
    (void)routing_key; (void)dest_sock; (void)src_override; (void)src_node;
    (void)src_sock; (void)pkt_info; (void)tpl; (void)extra;
    mock_send_calls++;
    mock_send_tpl_len    = tpl_len;
    mock_send_data_len   = data_len;
    mock_send_data       = data;
    mock_send_tpl        = tpl;
    mock_send_request_id = request_id;
    mock_send_dest_node  = dest_node;
    capture_response(tpl);
    *len_out = tpl_len;
    *status = status_$ok;
}

/* When set, DIR_$SERVER stamps this byte all over the reply's bulk area
 * (reply+0x200), the region 0x00E6417C hands to PKT_$SEND_INTERNET. */
static uint8_t mock_dir_server_bulk_fill;

void DIR_$SERVER(void *req, void *resp, uint16_t *reply_len)
{
    (void)req;
    mock_dir_server_calls++;
    if (mock_dir_server_bulk_fill != 0) {
        memset((uint8_t *)resp + 0x200, mock_dir_server_bulk_fill, 0x100);
    }
    *reply_len = mock_dir_server_reply_len;
}

void ACL_$SERVER(void *req, void *resp, uint16_t *reply_len)
{
    (void)req; (void)resp;
    mock_acl_server_calls++;
    *reply_len = mock_acl_server_reply_len;
}

void CRASH_SYSTEM(const status_$t *s) { (void)s; mock_crash_calls++; }
void CRASH_SHOW_STRING(const char *s) { (void)s; }

void NETLOG_$LOG_IT(uint16_t kind, uint32_t *uid, uint16_t p3, uint16_t p4,
                    uint16_t p5, uint16_t p6, uint16_t p7, uint16_t p8)
{
    (void)kind; (void)uid; (void)p3; (void)p4; (void)p7; (void)p8;
    mock_netlog_calls++;
    mock_netlog_p5 = p5;
    mock_netlog_p6 = p6;
}

int8_t FILE_$NEIGHBORS(uid_t *a, uid_t *b, status_$t *st)
{
    (void)a; (void)b; mock_neighbors_calls++; *st = status_$ok; return -1;
}

void FILE_$PRIV_LOCK(uid_t *u, int16_t asid, uint16_t side, uint16_t mode,
                     boolean local_only, uint16_t flags, uint16_t key,
                     uint32_t rk, uint32_t rn, uint32_t re, void **acl,
                     uint16_t rw, uint32_t *slot, uint16_t *rights,
                     status_$t *st)
{
    (void)u; (void)asid; (void)side; (void)mode; (void)local_only;
    (void)flags; (void)key; (void)rk; (void)rn; (void)re; (void)acl;
    (void)rw; (void)slot; (void)rights;
    *st = status_$ok;
}

boolean FILE_$PRIV_UNLOCK(uid_t *u, int32_t slot, uint16_t mode, uint16_t asid,
                          boolean by_key, uint16_t key,
                          uint32_t rem_key, uint32_t rem_node,
                          uint32_t *dtv, status_$t *st)
{
    (void)u; (void)slot; (void)mode; (void)asid; (void)by_key; (void)key;
    (void)rem_key; (void)rem_node; (void)dtv;
    *st = status_$ok;
    return 0;
}

uint32_t FILE_$PRIV_CREATE(int16_t t, const uid_t *tu, uid_t *du, uid_t *fu,
                           uint32_t sz, uint16_t fl, uid_t *oi, status_$t *st)
{
    (void)t; (void)tu; (void)du; (void)fu; (void)sz; (void)fl; (void)oi;
    *st = status_$ok;
    return 0;
}

void FILE_$LOCAL_READ_LOCK(uid_t *u, file_lock_info_internal_t *info,
                           status_$t *st)
{ (void)u; (void)info; *st = status_$ok; }

void FILE_$LOCAL_LOCK_VERIFY(lock_verify_request_t *r, status_$t *st)
{ (void)r; *st = status_$ok; }

void FILE_$READ_LOCK_ENTRYI(uid_t *u, uint16_t *idx,
                            file_lock_info_internal_t *info, status_$t *st)
{ (void)u; (void)idx; (void)info; *st = file_$object_not_found; }

void FILE_$DELETE(uid_t *u, status_$t *st) { (void)u; *st = status_$ok; }

void FILE_$SET_PROT_INT(uid_t *u, void *a, uint16_t t, uint16_t p,
                        boolean s, status_$t *st)
{ (void)u; (void)a; (void)t; (void)p; (void)s; *st = status_$ok; }

void FILE_$SET_ATTRIBUTE(uid_t *u, int16_t id, void *v, uint16_t rights,
                         int16_t options, status_$t *st)
{ (void)u; (void)id; (void)v; (void)rights; (void)options; *st = status_$ok; }

void AST_$GET_ATTRIBUTES(file_$obj_loc_t *u, uint16_t fl, void *a,
                         status_$t *st)
{ (void)u; (void)fl; memset(a, 0, 0x90); *st = status_$ok; }
void AST_$GET_ACL_ATTRIBUTES(file_$obj_loc_t *u, uint16_t fl,
                             ast_$acl_attr_t *a, status_$t *st)
{ (void)u; (void)fl; memset(a, 0, sizeof(*a)); *st = status_$ok; }
void AST_$SET_ATTRIBUTE(uid_t *u, uint16_t id, void *v, status_$t *st)
{ (void)u; (void)id; (void)v; *st = status_$ok; }
uint16_t AST_$PURIFY(uid_t *u, uint16_t fl, int16_t sg, uint32_t *sl,
                     uint16_t un, status_$t *st)
{ (void)u; (void)fl; (void)sg; (void)sl; (void)un; *st = status_$ok; return 0; }
void AST_$GET_DTV(uid_t *u, uint32_t un, uint32_t *dtv, status_$t *st)
{ (void)u; (void)un; (void)dtv; *st = status_$ok; }
void AST_$GET_SEG_MAP(uid_t *ui, uint32_t off, uint32_t un, uint32_t sc,
                      uint32_t ms, uint16_t fl, uint32_t *out, status_$t *st)
{ (void)ui; (void)off; (void)un; (void)sc; (void)ms; (void)fl; (void)out;
  *st = status_$ok; }
void AST_$INVALIDATE(uid_t *u, uint32_t sp, uint32_t c, int16_t fl,
                     status_$t *st)
{ (void)u; (void)sp; (void)c; (void)fl; *st = status_$ok; }
void AST_$RESERVE(uid_t *u, uint32_t sb, uint32_t bc, status_$t *st)
{ (void)u; (void)sb; (void)bc; *st = status_$ok; }
void AST_$TRUNCATE(uid_t *u, uint32_t sz, uint16_t fl, uint8_t *res,
                   status_$t *st)
{ (void)u; (void)sz; (void)fl; (void)res; mock_truncate_calls++;
  *st = status_$ok; }
void AST_$GET_LOCATION(file_$obj_loc_t *ui, uint16_t fl, uint32_t *un,
                       uint32_t *vu, status_$t *st)
{ (void)ui; (void)fl; (void)un; (void)vu; *st = file_$object_not_found; }

void UID_$GEN(uid_t *u) { u->high = 0x11223344; u->low = 0x55667788; }

void ACL_$ENTER_SUPER(void) {}
void ACL_$EXIT_SUPER(void) {}
void AUDIT_$SUSPEND(void) {}
void AUDIT_$RESUME(void) {}
void ACL_$GET_RE_ALL_SIDS(void *a, void *b, void *c, void *d,
                          status_$t *st)
{ (void)a; (void)b; (void)c; (void)d; *st = status_$ok; }
void ACL_$SET_RE_ALL_SIDS(void *a, void *b, void *c, void *d, status_$t *st)
{ (void)a; (void)b; (void)c; (void)d; *st = status_$ok; }
void ACL_$GET_PROJ_LIST(uid_t *a, int16_t *b, int16_t *c, status_$t *st)
{ (void)a; (void)b; (void)c; *st = status_$ok; }
void ACL_$SET_PROJ_LIST(uid_t *a, int16_t *b, status_$t *st)
{ (void)a; (void)b; *st = status_$ok; }
void ACL_$OVERRIDE_LOCAL_LOCKSMITH(int16_t e, status_$t *st)
{ (void)e; *st = status_$ok; }
void ACL_$CONVERT_FUNKY_ACL(void *a, void *b, void *c, void *d, status_$t *st)
{ (void)a; (void)b; (void)c; (void)d; *st = status_$ok; }
int8_t ACL_$CONVERT_TO_10ACL(void *a, void *b, uid_t *c, void *d,
                             status_$t *st)
{ (void)a; (void)b; (void)c; (void)d; *st = status_$ok; return 0; }

void DIR_$GET_ENTRYU(uid_t *d, char *n, uint16_t *nl, void *e, status_$t *st)
{ (void)d; (void)n; (void)nl; (void)e; *st = status_$ok; }
void DIR_$OLD_SET_DEFAULT_ACL(uid_t *a, uid_t *b, uid_t *c, status_$t *st)
{ (void)a; (void)b; (void)c; *st = status_$ok; }
void DIR_$OLD_ADD_HARD_LINKU(uid_t *d, char *n, uint16_t *nl, uid_t *t,
                             status_$t *st)
{ (void)d; (void)n; (void)nl; (void)t; *st = status_$ok; }
void DIR_$OLD_DROP_HARD_LINKU(uid_t *d, char *n, uint16_t *nl, uint16_t *fl,
                              status_$t *st)
{ (void)d; (void)n; (void)nl; (void)fl; *st = status_$ok; }
void DIR_$DROP_MOUNT(uid_t *mp, uid_t *d, uint32_t *lv, status_$t *st)
{ (void)mp; (void)d; (void)lv; *st = status_$ok; }

uint16_t AREA_$CREATE_FROM(uint32_t ru, uint32_t vs, uint32_t cs,
                           int32_t cid, status_$t *st)
{ (void)ru; (void)vs; (void)cs; (void)cid; *st = status_$ok; return 7; }
void AREA_$DELETE_FROM(uint16_t ix, uint32_t ru, uint32_t cid, status_$t *st)
{ (void)ix; (void)ru; (void)cid; *st = status_$ok; }
void AREA_$GROW_TO(uint16_t ix, uint32_t vs, uint32_t cs, status_$t *st)
{ (void)ix; (void)vs; (void)cs; *st = status_$ok; }
void AREA_$FREE_FROM(uint32_t n) { (void)n; }

void MAP_CASE(char *n, int16_t *nl, char *o, int16_t *m, int16_t *ol,
              uint8_t *t)
{ (void)n; (void)m; *ol = *nl; (void)o; *t = 0; }
void UNMAP_CASE(char *n, int16_t *nl, char *o, int16_t *m, int16_t *ol,
                uint8_t *t)
{ (void)n; (void)m; (void)o; *ol = *nl; *t = 0; }

/* ============================================================================
 * Code under test
 * ============================================================================ */

#include "../server.c"

/* ============================================================================
 * Fixture
 * ============================================================================ */

#define TEST_PEER_NODE  0x000ABCDE
#define TEST_REQUEST_ID 0x1234

static uint8_t service_info[0x20];

static void set_request(uint16_t version, uint8_t opcode,
                        uint16_t body_len, uint16_t extra_len)
{
    memset(mock_req_src, 0, sizeof(mock_req_src));
    mock_req_src[0] = (uint8_t)(version >> 8);
    mock_req_src[1] = (uint8_t)(version & 0xFF);
    mock_req_src[3] = opcode;
    /* The server reads the request as a native record, so write the version
     * word natively too. */
    *(uint16_t *)(void *)mock_req_src = version;

    *(uint16_t *)(void *)(mock_pkt_hdr + 0x02) = body_len;
    *(uint16_t *)(void *)(mock_pkt_hdr + 0x04) = extra_len;
}

static void reset_world(void)
{
    memset(mock_pkt_hdr, 0, sizeof(mock_pkt_hdr));
    memset(&mock_rcv_template, 0, sizeof(mock_rcv_template));
    memset(service_info, 0, sizeof(service_info));
    memset(network_file_backlog_tail, 0, sizeof(network_file_backlog_tail));

    memset(NETWORK_$FILE_BACKLOG, 0, sizeof(NETWORK_$FILE_BACKLOG));
    NETWORK_$SERVICE_INFO_PTR = service_info;
    NETWORK_$ALLOWED_SERVICE = (uint32_t)NETWORK_CAP_FILE_SERVER << 16;
    NETWORK_$DISKLESS = 0;
    NETWORK_$REALLY_DISKLESS = 0;
    NETWORK_$MOTHER_NODE = 0;
    NETLOG_$OK_TO_LOG_SERVER = 0;
    REM_FILE_$STALE_LINK_COUNT = 0;

    mock_recv_status = status_$ok;
    mock_send_calls = 0;
    mock_send_tpl_len = 0;
    mock_send_data_len = 0;
    mock_send_data = NULL;
    mock_send_tpl = NULL;
    mock_dir_server_bulk_fill = 0;
    mock_dir_server_calls = 0;
    mock_acl_server_calls = 0;
    mock_dir_server_reply_len = 0x40;
    mock_acl_server_reply_len = 0x40;
    mock_excl_start = 0;
    mock_excl_stop = 0;
    mock_crash_calls = 0;
    mock_rtn_hdr_calls = 0;
    mock_dump_calls = 0;
    mock_dump_len = 0;
    mock_dat_copy_calls = 0;
    mock_dat_copy_len = 0;
    mock_getva_calls = 0;
    mock_rtn_dat_calls = 0;
    mock_get_dat_calls = 0;
    mock_getva_result = 0x00A00000;
    mock_netlog_calls = 0;
    mock_neighbors_calls = 0;
    mock_truncate_calls = 0;

    /* Packet header record the receive block points at. */
    *(uint16_t *)(void *)(mock_pkt_hdr + 0x06) = TEST_REQUEST_ID;
    *(uint32_t *)(void *)(mock_pkt_hdr + 0x08) = 0x11111111;
    *(uint32_t *)(void *)(mock_pkt_hdr + 0x0E) = TEST_PEER_NODE;
    *(uint16_t *)(void *)(mock_pkt_hdr + 0x12) = 3;

    /* APP_$RECEIVE result block. */
    mock_rcv_template.hdr  = mock_pkt_hdr;
    mock_rcv_template.data = mock_req_src;
    /* bufs[0] == 0 => the request carries no extra buffer chain. */
    mock_rcv_template.bufs[0] = 0;

    set_request(1, REM_FILE_OP_TEST, 0x10, 0);
}

/*
 * The frame is a local of REM_FILE_$SERVER, so the tests observe it through
 * the mocked PKT_$SEND_INTERNET arguments and the response header, which the
 * send mock captures by copying the template pointer's first bytes.
 */
#define SENT_PKT_FLAG   (sent_resp.pkt_flag)
#define SENT_MAGIC      (sent_resp.magic)
#define SENT_OPCODE     (sent_resp.opcode)
#define SENT_STATUS     (sent_resp.status)

/* ============================================================================
 * Tests: receive path
 * ============================================================================ */

TEST(empty_queue_sends_nothing)
{
    reset_world();
    mock_recv_status = rem_file_$rcv_queue_empty;
    REM_FILE_$SERVER();
    ASSERT_EQ(0x00110006, rem_file_$rcv_queue_empty);
    ASSERT_EQ(0, mock_send_calls);
    /* The exclusion lock was taken and released exactly once. */
    ASSERT_EQ(1, mock_excl_start);
    ASSERT_EQ(1, mock_excl_stop);
}

TEST(receive_error_sends_nothing)
{
    reset_world();
    mock_recv_status = 0x00990001;
    REM_FILE_$SERVER();
    ASSERT_EQ(0, mock_send_calls);
    ASSERT_EQ(1, mock_excl_stop);
}

TEST(service_disabled_replies_0x0011000F)
{
    reset_world();
    NETWORK_$ALLOWED_SERVICE = 0;
    REM_FILE_$SERVER();
    ASSERT_EQ(1, mock_send_calls);
    ASSERT_EQ(8, mock_send_tpl_len);
    ASSERT_EQ(0x0011000F, SENT_STATUS);
    ASSERT_EQ(0x0011000F, rem_file_$service_not_enabled);
    /* The lock is still held when the service is off, so it is dropped once
     * on the way out (0x00E6455A). */
    ASSERT_EQ(1, mock_excl_stop);
}

TEST(request_length_is_clamped_to_0x294)
{
    reset_world();
    set_request(1, REM_FILE_OP_TEST, 0x400, 0);
    REM_FILE_$SERVER();
    /* Nothing observable escapes, but the copy must not have overrun: the
     * reply is still the plain 8-byte acknowledgement. */
    ASSERT_EQ(1, mock_send_calls);
    ASSERT_EQ(8, mock_send_tpl_len);
}

TEST(backlog_histogram_indexes_by_depth_byte)
{
    reset_world();
    service_info[0x15] = 3;
    REM_FILE_$SERVER();
    ASSERT_EQ(1, NETWORK_$FILE_BACKLOG[3]);
    ASSERT_EQ(0, NETWORK_$FILE_BACKLOG_OVERFLOW);

    /*
     * Depth 8 and the "deeper than 8" branch land on the SAME longword:
     * 0x00E63644 bumps 0xE24BD0 + 4*depth and 0x00E6364E bumps 0xE24BF0,
     * which is 0xE24BD0 + 4*8.  NETWORK_$FILE_BACKLOG_OVERFLOW is therefore
     * bucket 8, not a cell of its own (network/network.h).
     */
    reset_world();
    service_info[0x15] = 8;
    REM_FILE_$SERVER();
    ASSERT_EQ(1, NETWORK_$FILE_BACKLOG[8]);
    ASSERT_EQ(1, NETWORK_$FILE_BACKLOG_OVERFLOW);

    reset_world();
    service_info[0x15] = 9;
    REM_FILE_$SERVER();
    ASSERT_EQ(1, NETWORK_$FILE_BACKLOG_OVERFLOW);
}

TEST(oversized_extra_data_is_dumped_and_refused)
{
    reset_world();
    set_request(1, SERVER_OP_DIR_GET_ENTRY, 0x10, 0x401);
    mock_rcv_template.bufs[0] = 0x1234;
    REM_FILE_$SERVER();
    ASSERT_EQ(1, mock_dump_calls);
    ASSERT_EQ(0x401, (uint16_t)mock_dump_len);
    ASSERT_EQ(0, mock_send_calls);
    ASSERT_EQ(0x0011001C, rem_file_$request_too_long);
}

TEST(inline_extra_data_is_appended_and_dumped)
{
    reset_world();
    set_request(1, REM_FILE_OP_TEST, 0x100, 0x300);
    mock_rcv_template.bufs[0] = 0x1234;
    REM_FILE_$SERVER();
    /* min(extra_len, 0x294 - request_len) = min(0x300, 0x194) */
    ASSERT_EQ(1, mock_dat_copy_calls);
    ASSERT_EQ(0x194, mock_dat_copy_len);
    ASSERT_EQ(1, mock_dump_calls);
    ASSERT_EQ(0x300, (uint16_t)mock_dump_len);
}

/* ============================================================================
 * Tests: response header and dispatch
 * ============================================================================ */

TEST(response_header_is_magic_and_opcode_plus_one)
{
    reset_world();
    set_request(1, REM_FILE_OP_NEIGHBORS, 0x20, 0);
    REM_FILE_$SERVER();
    ASSERT_EQ(1, mock_send_calls);
    ASSERT_EQ(0x80, SENT_MAGIC);
    ASSERT_EQ(REM_FILE_OP_NEIGHBORS + 1, SENT_OPCODE);
    ASSERT_EQ(1, SENT_PKT_FLAG);
    ASSERT_EQ(1, mock_neighbors_calls);
    ASSERT_EQ(0x0A, mock_send_tpl_len);
}

TEST(bad_version_is_answered_as_opcode_two)
{
    reset_world();
    set_request(9, REM_FILE_OP_NEIGHBORS, 0x20, 0);
    REM_FILE_$SERVER();
    /* 0x00E63810 rewrites the opcode to 2, which no case handles. */
    ASSERT_EQ(1, mock_send_calls);
    ASSERT_EQ(0x03, SENT_OPCODE);
    ASSERT_EQ(file_$bad_reply_received_from_remote_node, SENT_STATUS);
    ASSERT_EQ(8, mock_send_tpl_len);
    ASSERT_EQ(0, mock_neighbors_calls);
}

TEST(unknown_opcode_is_refused)
{
    reset_world();
    set_request(1, 0x9E, 0x20, 0);
    REM_FILE_$SERVER();
    ASSERT_EQ(0x03, SENT_OPCODE);
    ASSERT_EQ(0x000F0003, SENT_STATUS);
    ASSERT_EQ(8, mock_send_tpl_len);
}

TEST(test_opcode_replies_ok)
{
    reset_world();
    set_request(1, REM_FILE_OP_TEST, 0x20, 0);
    REM_FILE_$SERVER();
    ASSERT_EQ(status_$ok, SENT_STATUS);
    ASSERT_EQ(8, mock_send_tpl_len);
}

TEST(dir_range_is_delegated)
{
    for (unsigned op = REM_FILE_DIR_OP_FIRST; op <= REM_FILE_DIR_OP_LAST; op += 2) {
        if ((op == SERVER_OP_DIR_GET_ENTRY) || (op == SERVER_OP_DIR_READ_LINK) ||
            (op == SERVER_OP_DIR_READ_DIR)  || (op == SERVER_OP_DIR_LIST)) {
            continue;   /* these also juggle a netbuf; covered separately */
        }
        reset_world();
        set_request(1, (uint8_t)op, 0x20, 0);
        mock_dir_server_reply_len = 0x30;
        REM_FILE_$SERVER();
        ASSERT_EQ(1, mock_dir_server_calls);
        ASSERT_EQ(0, mock_acl_server_calls);
        ASSERT_EQ(0x30, mock_send_tpl_len);
    }
}

TEST(acl_range_is_delegated)
{
    for (unsigned op = REM_FILE_ACL_OP_FIRST; op <= REM_FILE_ACL_OP_LAST; op += 2) {
        if ((op == REM_FILE_OP_ACL_IMAGE) || (op == REM_FILE_OP_ACL_CREATE)) {
            continue;
        }
        reset_world();
        set_request(1, (uint8_t)op, 0x20, 0);
        mock_acl_server_reply_len = 0x34;
        REM_FILE_$SERVER();
        ASSERT_EQ(1, mock_acl_server_calls);
        ASSERT_EQ(0, mock_dir_server_calls);
        ASSERT_EQ(0x34, mock_send_tpl_len);
    }
}

TEST(range_boundaries_are_inclusive)
{
    reset_world();
    set_request(1, REM_FILE_DIR_OP_FIRST - 2, 0x20, 0);   /* 0x28 = drop link */
    REM_FILE_$SERVER();
    ASSERT_EQ(0, mock_dir_server_calls);

    reset_world();
    set_request(1, REM_FILE_DIR_OP_LAST, 0x20, 0);        /* 0x5C */
    REM_FILE_$SERVER();
    ASSERT_EQ(1, mock_dir_server_calls);

    reset_world();
    set_request(1, REM_FILE_ACL_OP_LAST, 0x20, 0);        /* 0x77 */
    REM_FILE_$SERVER();
    ASSERT_EQ(1, mock_acl_server_calls);

    reset_world();
    set_request(1, REM_FILE_ACL_OP_LAST + 1, 0x20, 0);    /* 0x78: unknown */
    REM_FILE_$SERVER();
    ASSERT_EQ(0, mock_acl_server_calls);
    ASSERT_EQ(0x000F0003, SENT_STATUS);
}

TEST(opcode_0x7c_is_reserve_not_a_delegation)
{
    reset_world();
    set_request(1, REM_FILE_OP_RESERVE, 0x20, 0);
    REM_FILE_$SERVER();
    ASSERT_EQ(0, mock_dir_server_calls);
    ASSERT_EQ(0, mock_acl_server_calls);
    ASSERT_EQ(status_$ok, SENT_STATUS);
    ASSERT_EQ(8, mock_send_tpl_len);
}

TEST(node_crash_keeps_the_lock_and_sends_no_reply)
{
    reset_world();
    set_request(1, REM_FILE_OP_UNLOCK_ALL, 0x20, 0);
    REM_FILE_$SERVER();
    ASSERT_EQ(0, mock_send_calls);
    /* The lock is never dropped mid-flight for this opcode, so the unwind
     * path releases it exactly once (0x00E6381E / 0x00E64578). */
    ASSERT_EQ(1, mock_excl_stop);
}

TEST(per_opcode_reply_lengths)
{
    struct { uint8_t op; uint16_t len; uint16_t req_len; } cases[] = {
        { REM_FILE_OP_TEST,              0x08, 0x20 },
        { REM_FILE_OP_NEIGHBORS,         0x0A, 0x20 },
        { REM_FILE_OP_LOCAL_READ_LOCK,   0x2A, 0x20 },
        { REM_FILE_OP_GET_SEG_MAP,       0x28, 0x20 },
        { REM_FILE_OP_GENERATE_UID,      0x12, 0x20 },
        { REM_FILE_OP_CREATE_TYPE_PRESR10,    0x12, 0x20 },
        { REM_FILE_OP_CREATE_TYPE,       0xBE, 0x20 },
        { REM_FILE_OP_UNLOCK,            0x16, 0x20 },
        { REM_FILE_OP_CREATE_AREA,       0x0C, 0x20 },
        { REM_FILE_OP_DELETE_AREA,       0x08, 0x20 },
        { REM_FILE_OP_LOCK,              0x10, 0x20 },
        { REM_FILE_OP_LOCK,              0x0E, 0x1E },
        { REM_FILE_OP_LOCK_EXT,     0xBE, 0x20 },
        { REM_FILE_OP_INVALIDATE,        0x08, 0x20 },
        { REM_FILE_OP_PURIFY,            0x08, 0x20 },
        { REM_FILE_OP_LOCAL_VERIFY, 0x08, 0x20 },
    };

    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        reset_world();
        set_request(1, cases[i].op, cases[i].req_len, 0);
        REM_FILE_$SERVER();
        if (mock_send_tpl_len != cases[i].len) {
            if (current_failed == 0) { printf("FAILED\n"); }
            printf("      opcode 0x%02X: expected reply 0x%X, got 0x%X\n",
                   cases[i].op, cases[i].len, mock_send_tpl_len);
            current_failed = 1; tests_failed++;
            return;
        }
    }
}

/* ============================================================================
 * Tests: the response path
 * ============================================================================ */

TEST(reply_longer_than_0x200_is_split)
{
    reset_world();
    set_request(1, REM_FILE_DIR_OP_FIRST + 2, 0x20, 0);
    mock_dir_server_reply_len = 0x280;
    REM_FILE_$SERVER();
    ASSERT_EQ(1, mock_send_calls);
    ASSERT_EQ(0x200, mock_send_tpl_len);
    ASSERT_EQ(0x80, (uint16_t)mock_send_data_len);
}

/*
 * 0x00E6417C `lea (0x60,A6),A4`: the bulk pointer is the reply record plus
 * 0x200 (the reply is based at A6-0x1A0), which runs past the end of
 * REM_FILE_$SERVER's own frame.  The C model keeps that memory in the frame
 * object so the pointer is a real, writable address - it used to be NULL.
 */
TEST(split_reply_bulk_is_the_reply_record_plus_0x200)
{
    reset_world();
    set_request(1, REM_FILE_DIR_OP_FIRST + 2, 0x20, 0);
    mock_dir_server_reply_len = 0x280;
    mock_dir_server_bulk_fill = 0xA5;
    REM_FILE_$SERVER();

    ASSERT_EQ(1, mock_send_calls);
    ASSERT_EQ(0x200, mock_send_tpl_len);
    ASSERT_EQ(0x80, (uint16_t)mock_send_data_len);
    ASSERT_EQ(1, mock_send_data != NULL);
    ASSERT_EQ(1, mock_send_data == (void *)((uint8_t *)mock_send_tpl + 0x200));
    /* The bytes DIR_$SERVER put there are the bytes that go out. */
    ASSERT_EQ(0xA5, ((const uint8_t *)mock_send_data)[0]);
    ASSERT_EQ(0xA5, ((const uint8_t *)mock_send_data)[0x7F]);
}

/*
 * The unsplit path leaves A4 alone, so the bulk pointer is still whatever the
 * dispatch put there - a netbuf address, or NULL when no netbuf was taken.
 */
TEST(unsplit_reply_leaves_the_bulk_pointer_alone)
{
    reset_world();
    set_request(1, REM_FILE_DIR_OP_FIRST + 2, 0x20, 0);
    mock_dir_server_reply_len = 0x40;
    REM_FILE_$SERVER();

    ASSERT_EQ(1, mock_send_calls);
    ASSERT_EQ(0x40, mock_send_tpl_len);
    ASSERT_EQ(0, (uint16_t)mock_send_data_len);
    ASSERT_EQ(1, mock_send_data == NULL);
}

TEST(reply_exactly_0x200_is_not_split)
{
    reset_world();
    set_request(1, REM_FILE_DIR_OP_FIRST + 2, 0x20, 0);
    mock_dir_server_reply_len = 0x200;
    REM_FILE_$SERVER();
    ASSERT_EQ(0x200, mock_send_tpl_len);
    ASSERT_EQ(0, (uint16_t)mock_send_data_len);
}

TEST(bulk_over_0x400_raises_0x00110001)
{
    reset_world();
    set_request(1, REM_FILE_DIR_OP_FIRST + 2, 0x20, 0);
    mock_dir_server_reply_len = 0x601;    /* bulk = 0x401 */
    REM_FILE_$SERVER();
    ASSERT_EQ(0, mock_send_calls);        /* the send is skipped entirely */
    ASSERT_EQ(0x00110001, rem_file_$reply_too_long);
}

TEST(send_arguments_come_from_the_packet_header)
{
    reset_world();
    set_request(1, REM_FILE_OP_TEST, 0x20, 0);
    REM_FILE_$SERVER();
    ASSERT_EQ(1, mock_send_calls);
    ASSERT_EQ(TEST_REQUEST_ID, mock_send_request_id);
    ASSERT_EQ(TEST_PEER_NODE, mock_send_dest_node);
}

TEST(netlog_is_only_written_when_server_logging_is_on)
{
    reset_world();
    set_request(1, REM_FILE_OP_TEST, 0x20, 0);
    REM_FILE_$SERVER();
    ASSERT_EQ(0, mock_netlog_calls);

    reset_world();
    NETLOG_$OK_TO_LOG_SERVER = -1;
    set_request(1, REM_FILE_OP_TRUNCATE, 0x20, 0);
    REM_FILE_$SERVER();
    ASSERT_EQ(1, mock_netlog_calls);
}

/* ============================================================================
 * Tests: the NETLOG opcode map
 * ============================================================================ */

TEST(log_code_table)
{
    ASSERT_EQ(0x0B, server_log_code(0x00));
    ASSERT_EQ(0x12, server_log_code(0x02));
    ASSERT_EQ(0x01, server_log_code(0x04));
    ASSERT_EQ(0x00, server_log_code(0x06));
    ASSERT_EQ(0x03, server_log_code(0x08));
    ASSERT_EQ(0x04, server_log_code(0x0A));
    ASSERT_EQ(0x05, server_log_code(0x0C));
    ASSERT_EQ(0x09, server_log_code(0x0E));
    ASSERT_EQ(0x02, server_log_code(0x10));
    ASSERT_EQ(0x06, server_log_code(0x12));
    ASSERT_EQ(0x0A, server_log_code(0x14));
    ASSERT_EQ(0x07, server_log_code(0x16));
    ASSERT_EQ(0x24, server_log_code(0x18));
    ASSERT_EQ(0x08, server_log_code(0x1A));
    ASSERT_EQ(0x0D, server_log_code(0x1C));
    ASSERT_EQ(0x0E, server_log_code(0x1E));
    ASSERT_EQ(0x0F, server_log_code(0x20));
    ASSERT_EQ(0x3D, server_log_code(0x7C));
    ASSERT_EQ(0x10, server_log_code(0x22));
    ASSERT_EQ(0x11, server_log_code(0x62));
    ASSERT_EQ(0x3A, server_log_code(0x86));
    ASSERT_EQ(0x3B, server_log_code(0x88));
    ASSERT_EQ(0x3C, server_log_code(0x8A));
    ASSERT_EQ(0x3E, server_log_code(0x7E));
    ASSERT_EQ(0x3F, server_log_code(0x80));
    ASSERT_EQ(0x40, server_log_code(0x82));
    ASSERT_EQ(0x41, server_log_code(0x84));
    ASSERT_EQ(0x42, server_log_code(0x24));
    ASSERT_EQ(0x1E, server_log_code(0x28));
}

TEST(log_code_folded_ranges)
{
    /* 0x00E644AC: (op - 0x2A) / 2 + 0x13 over 0x2A..0x5C */
    ASSERT_EQ(0x13, server_log_code(0x2A));
    ASSERT_EQ(0x14, server_log_code(0x2C));
    ASSERT_EQ(0x2C, server_log_code(0x5C));   /* (0x5C-0x2A)/2 + 0x13 */
    /* 0x00E644D8: (op - 0x64) / 2 + 0x2F over 0x64..0x77 */
    ASSERT_EQ(0x2F, server_log_code(0x64));
    ASSERT_EQ(0x30, server_log_code(0x66));
    ASSERT_EQ(0x38, server_log_code(0x76));
    /* Anything else keeps the 0x12 the code stores at 0x00E6428E. */
    ASSERT_EQ(0x12, server_log_code(0x9E));
}

/* ============================================================================
 * main
 * ============================================================================ */

int main(void)
{
    printf("REM_FILE_$SERVER (0x00E63586) tests\n");

    RUN_TEST(empty_queue_sends_nothing);
    RUN_TEST(receive_error_sends_nothing);
    RUN_TEST(service_disabled_replies_0x0011000F);
    RUN_TEST(request_length_is_clamped_to_0x294);
    RUN_TEST(backlog_histogram_indexes_by_depth_byte);
    RUN_TEST(oversized_extra_data_is_dumped_and_refused);
    RUN_TEST(inline_extra_data_is_appended_and_dumped);

    RUN_TEST(response_header_is_magic_and_opcode_plus_one);
    RUN_TEST(bad_version_is_answered_as_opcode_two);
    RUN_TEST(unknown_opcode_is_refused);
    RUN_TEST(test_opcode_replies_ok);
    RUN_TEST(dir_range_is_delegated);
    RUN_TEST(acl_range_is_delegated);
    RUN_TEST(range_boundaries_are_inclusive);
    RUN_TEST(opcode_0x7c_is_reserve_not_a_delegation);
    RUN_TEST(node_crash_keeps_the_lock_and_sends_no_reply);
    RUN_TEST(per_opcode_reply_lengths);

    RUN_TEST(reply_longer_than_0x200_is_split);
    RUN_TEST(split_reply_bulk_is_the_reply_record_plus_0x200);
    RUN_TEST(unsplit_reply_leaves_the_bulk_pointer_alone);
    RUN_TEST(reply_exactly_0x200_is_not_split);
    RUN_TEST(bulk_over_0x400_raises_0x00110001);
    RUN_TEST(send_arguments_come_from_the_packet_header);
    RUN_TEST(netlog_is_only_written_when_server_logging_is_on);

    RUN_TEST(log_code_table);
    RUN_TEST(log_code_folded_ranges);

    printf("%d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
