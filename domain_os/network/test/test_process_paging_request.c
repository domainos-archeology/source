/*
 * network/test/test_process_paging_request.c - unit tests for
 * NETWORK_$PROCESS_PAGING_REQUEST (0x00E10628)
 *
 * The page server's frame is a local here; the reply sender is a mock that
 * snapshots the reply, the data page and the status each time it is called.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;

static void reset_state(void);

#define RUN_TEST(name) do { \
    printf("  Running %s... ", #name); \
    reset_state(); \
    test_##name(); \
    printf("PASSED\n"); \
    tests_passed++; \
} while (0)

#define ASSERT_EQ(expected, actual) do { \
    if ((unsigned long)(expected) != (unsigned long)(actual)) { \
        printf("FAILED\n    Expected: 0x%lx, Got: 0x%lx at line %d\n", \
               (unsigned long)(expected), (unsigned long)(actual), __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

#include "network/network_internal.h"
#include "area/area.h"
#include "ast/ast.h"
#include "anon/anon.h"
#include "math/math.h"
#include "ml/ml.h"
#include "mmap/mmap.h"
#include "netbuf/netbuf.h"
#include "netlog/netlog.h"
#include "os/os.h"
#include "pkt/pkt.h"
#include "proc1/proc1.h"
#include "ring/ring.h"
#include "time/time.h"
#include "vtoc/vtoc.h"
#include "wp/wp.h"
#include "misc/crash_system.h"

/* ---- data ------------------------------------------------------------ */

MODULE_DATA_DEFINE(sock_$data_t, SOCK_$DATA, 0x00E27510);
MODULE_DATA_DEFINE(proc1_$data_t, PROC1_$DATA, 0x00E254E8);
MODULE_DATA_DEFINE(mmap_$mmape_table_t, MMAP_$MMAPE, 0x00EB4800);
MODULE_DATA_DEFINE(ring_$wired_data_t, RING_$WIRED_DATA, 0x00E261AC);
uid_t ANON_$UID = { 0xAAAA0000u, 0 };
int8_t NETLOG_$OK_TO_LOG_SERVER;
uint16_t PROC1_$CURRENT;
uint32_t NETWORK_$ALLOWED_SERVICE;
uint32_t NETWORK_$ZERO_PAGE_PA;
uint32_t NETWORK_$PAGING_BACKLOG[NETWORK_PAGING_BACKLOG_BUCKETS];
uint16_t NETWORK_$MULT_PAGIN_RQST_CNT, NETWORK_$BAD_CHKSUM_CNT;
uint16_t NETWORK_$ATTRIB_RQST_CNT, NETWORK_$INFO_RQST_CNT, NETWORK_$PAGOUT_RQST_CNT;
uint16_t NETWORK_$CLEAR_WIRED, NETWORK_$2LONG1;
int16_t NETWORK_$SERVICE_TIME;
char NETWORK_$DO_CHKSUM;
int8_t NETWORK_$DISKLESS;
uint32_t NETWORK_$MOTHER_NODE;
network_$failure_rec_t NETWORK_$FAILURE_REC;
const status_$t network_$read_ahead_chksum_status = 0x00110010;

/* ---- mocks ----------------------------------------------------------- */

static uint8_t arena[0x400];
static app_$receive_rec_t rcv_stub;
static status_$t rcv_status;
static uint32_t clock_now;
static int n_send, n_dump, n_wire, n_unwire, n_lock, n_unlock, n_rtn_dat;
static int n_crash, n_dts, n_log, n_assoc, n_new_to_old, n_setattr;
static uint16_t reply_type[8];
static status_$t reply_status[8];
static int16_t reply_seq[8];
static uint32_t reply_page[8];
static int16_t reply_len[8];
static uint32_t rtn_dat_pa;
static uint16_t touch_ret;
static status_$t touch_status, assoc_status, attr_status;
static aste_t aste;
static aote_t aote;
static uint32_t chksum_value;
static uint16_t pkt_size;
static uint16_t log_args[6];
static uint32_t log_uid_high;
static const status_$t *crash_arg;

void APP_$RECEIVE(uint16_t s, void *r, status_$t *st)
{ (void)s; memcpy(r, &rcv_stub, sizeof(rcv_stub)); *st = rcv_status; }
void TIME_$ABS_CLOCK(clock_t *c) { c->high = clock_now; c->low = 0x1234; clock_now += 1; }
void TIME_$CLOCK(clock_t *c) { c->high = 0x5555; c->low = 0x6666; }
void OS_$DATA_COPY(const void *s, void *d, uint32_t l) { memcpy(d, s, l); }
void NETBUF_$RTN_HDR(uint32_t *va) { (void)va; }
void NETBUF_$RTN_DAT(uint32_t pa) { n_rtn_dat++; rtn_dat_pa = pa; }
void PKT_$DUMP_DATA(uint32_t *b, int16_t l) { (void)b; (void)l; n_dump++; }
uint16_t NETWORK_$GET_PKT_SIZE(uint32_t *a, uint16_t m) { (void)a; (void)m; return pkt_size; }
void AREA_$TOUCH(area_$handle_t *h, uint16_t b, uint16_t s, int16_t p,
                 uint32_t *ppns, status_$t *st)
{ (void)h; (void)b; (void)s; (void)p; ppns[0] = 0x300; *st = touch_status; }
void AREA_$ASSOC(int16_t a, uint16_t b, int16_t p, uint32_t ppn, status_$t *st)
{ (void)a; (void)b; (void)p; (void)ppn; n_assoc++; *st = assoc_status; }
void MMAP_$WIRE(uint32_t v) { (void)v; n_wire++; }
void MMAP_$UNWIRE(uint32_t v) { (void)v; n_unwire++; }
void ML_$LOCK(int16_t id) { (void)id; n_lock++; }
void ML_$UNLOCK(int16_t id) { (void)id; n_unlock++; }
aste_t *AST_$ACTIVATE_AND_WIRE(uid_t *u, uint16_t s, status_$t *st)
{ (void)u; (void)s; *st = 0; aste.wire_count++; return &aste; }
uint16_t AST_$TOUCH(aste_t *a, uint32_t m, uint16_t p, uint16_t c,
                    uint32_t *ppns, status_$t *st, uint16_t f)
{
    uint16_t i;
    (void)a; (void)m; (void)p; (void)c; (void)f;
    for (i = 0; i < touch_ret; i++) ppns[i] = 0x400u + i;
    *st = touch_status;
    return touch_ret;
}
void AST_$GET_ATTRIBUTES(file_$obj_loc_t *l, uint16_t f, void *attrs, status_$t *st)
{
    network_$ps_attrs_t *a = (network_$ps_attrs_t *)attrs;
    (void)l; (void)f;
    memset(attrs, 0, 0x90);
    a->dtm.high = 0xD7D7; a->clock.high = 0xC1C1; a->acl.high = 0xACAC;
    *st = attr_status;
}
void AST_$ASSOC(uid_t *u, uint16_t s, uint32_t m, uint16_t p, uint16_t f,
                uint32_t ppn, status_$t *st)
{ (void)u; (void)s; (void)m; (void)p; (void)f; (void)ppn; n_assoc++; *st = assoc_status; }
uint8_t AST_$SET_DTS(uint16_t f, uid_t *u, uint32_t *d, uint32_t *a, status_$t *st)
{ (void)f; (void)u; (void)d; (void)a; n_dts++; *st = 0; return 0; }
void AST_$SET_ATTRIBUTE(uid_t *u, uint16_t id, void *v, status_$t *st)
{ (void)u; (void)id; (void)v; n_setattr++; *st = 0; }
ulong M$DIU$LLW(ulong a, ushort b) { return a / b; }
long M$DIS$LLL(long a, long b) { return a / b; }
uint32_t network_$page_chksum(uint32_t *d) { (void)d; return chksum_value; }
void CRASH_SYSTEM(const status_$t *s) { n_crash++; crash_arg = s; }
void WP_$CALLOC(uint32_t *ppn, status_$t *st) { *ppn = 0x777; *st = 0; }
void WP_$UNWIRE(uint32_t p) { (void)p; }
void network_$phys_copy(uint32_t d, uint32_t s, int16_t l) { (void)d; (void)s; (void)l; }
void VTOCE_$NEW_TO_OLD(void *n, char *f, void *o) { (void)n; (void)f; memset(o, 0x5A, 0x40); n_new_to_old++; }
void NETLOG_$LOG_IT(uint16_t kind, uint32_t *uid, uint16_t a3, uint16_t a4,
                    uint16_t a5, uint16_t a6, uint16_t a7, uint16_t a8)
{
    (void)kind;
    n_log++; log_uid_high = uid[0];
    log_args[0] = a3; log_args[1] = a4; log_args[2] = a5; log_args[3] = a6;
    log_args[4] = a7; log_args[5] = a8;
}
void network_$ps_send_reply(network_$ps_frame_t *ps)
{
    int i = n_send++ & 7;
    reply_type[i] = (uint16_t)ps->reply.type;
    reply_status[i] = ps->reply.hdr.status;
    reply_seq[i] = ps->reply.pagin.seq;
    reply_page[i] = ps->data_pages[0];
    reply_len[i] = ps->reply_len;
}

#include "../process_paging_request.c"

/* ---- helpers --------------------------------------------------------- */

static network_$ps_frame_t ps;
static sock_$sock_t sock1;
static network_$rcv_rec_t *rcv;
static network_$ps_rqst_t *rqst;         /* the request bytes APP hands in */

static void reset_state(void)
{
    memset(&ps, 0, sizeof(ps));
    memset(arena, 0, sizeof(arena));
    memset(&rcv_stub, 0, sizeof(rcv_stub));
    memset(&PROC1_$DATA, 0, sizeof(PROC1_$DATA));
    memset(NETWORK_$PAGING_BACKLOG, 0, sizeof(NETWORK_$PAGING_BACKLOG));
    ARCH_HOST_VA_BASE = (uintptr_t)arena - 0x10000u;
    rcv = (network_$rcv_rec_t *)(void *)arena;
    rqst = (network_$ps_rqst_t *)(void *)&arena[0x100];
    rcv_stub.reply = 0x10000;
    rcv_stub.data = 0x10100;
    rcv->rqst_len = 0x32;
    rcv->node_b = 0x00012345;
    rcv->data_len = 0x400;
    rcv_status = 0;
    SOCK_$DATA.socket_ptr[1] = &sock1;
    sock1.queue_count = 3;
    NETWORK_$ALLOWED_SERVICE = NETWORK_SERVICE_FILE_CAPABLE;
    NETWORK_$ZERO_PAGE_PA = 0xABC00;
    NETWORK_$MULT_PAGIN_RQST_CNT = NETWORK_$BAD_CHKSUM_CNT = 0;
    NETWORK_$ATTRIB_RQST_CNT = NETWORK_$INFO_RQST_CNT = NETWORK_$PAGOUT_RQST_CNT = 0;
    NETWORK_$CLEAR_WIRED = NETWORK_$2LONG1 = 0;
    NETWORK_$SERVICE_TIME = 0x14;
    NETWORK_$DO_CHKSUM = 0;
    NETLOG_$OK_TO_LOG_SERVER = 0;
    PROC1_$CURRENT = 2;
    n_send = n_dump = n_wire = n_unwire = n_lock = n_unlock = n_rtn_dat = 0;
    n_crash = n_dts = n_log = n_assoc = n_new_to_old = n_setattr = 0;
    touch_ret = 0; touch_status = assoc_status = attr_status = 0;
    memset(&aste, 0, sizeof(aste));
    memset(&aote, 0, sizeof(aote));
    aste.aote = &aote;
    clock_now = 100;
    pkt_size = 0x400;
    chksum_value = 0;
}

/* ---- tests ----------------------------------------------------------- */

static void test_receive_failure_does_nothing(void)
{
    rcv_status = 0x00110006;
    ps.data_pages[0] = 0x1234;
    NETWORK_$PROCESS_PAGING_REQUEST(&ps);
    ASSERT_EQ(0, ps.data_pages[0]);           /* cleared before the receive */
    ASSERT_EQ(0, n_send);
    ASSERT_EQ(0, NETWORK_$PAGING_BACKLOG[3]);
}

static void test_unknown_type(void)
{
    rqst->type = 3;
    rcv_stub.data_pages[0] = 0x2000;
    sock1.queue_count = 12;
    NETWORK_$PROCESS_PAGING_REQUEST(&ps);
    ASSERT_EQ(1, NETWORK_$PAGING_BACKLOG[8]);  /* depth > 8 -> last bucket */
    ASSERT_EQ(1, n_dump);
    ASSERT_EQ(1, n_send);
    ASSERT_EQ(9, reply_type[0]);
    ASSERT_EQ(0x0011000D, reply_status[0]);
    ASSERT_EQ(8, reply_len[0]);
    ASSERT_EQ(8, ps.reply.hdr.version);
    ASSERT_EQ(0x20, ps.pkt_flags);
    ASSERT_EQ(0x12345, ps.dest_node);
}

static void test_echo(void)
{
    rqst->type = 0;
    rqst->bytes[5] = 0x99;
    rcv->rqst_len = 0x10;
    rcv->flags = 0x13;
    NETWORK_$PROCESS_PAGING_REQUEST(&ps);
    ASSERT_EQ(1, NETWORK_$PAGING_BACKLOG[3]);
    ASSERT_EQ(1, reply_type[0]);
    ASSERT_EQ(0x10, reply_len[0]);
    ASSERT_EQ(0x99, ps.reply.bytes[5]);
    ASSERT_EQ(0x23, ps.pkt_flags);             /* bit 4 cleared, bit 5 set */
}

static void test_ring_info(void)
{
    rqst->type = 0xE;
    RING_$WIRED_DATA.xmit_esb = 0x4321;
    NETWORK_$MOTHER_NODE = 0x55;
    NETWORK_$DISKLESS = (int8_t)0xFF;
    NETWORK_$PROCESS_PAGING_REQUEST(&ps);
    ASSERT_EQ(0xF, reply_type[0]);
    ASSERT_EQ(0x80, reply_len[0]);
    ASSERT_EQ(3, ps.reply.ring.info._unknown_00);
    ASSERT_EQ(0x55, ps.reply.ring.info.mother_node);
    ASSERT_EQ(0x4321, ps.reply.ring.info.xmit_esb);
    ASSERT_EQ(1, NETWORK_$INFO_RQST_CNT);
}

static void test_pagin_denied(void)
{
    rqst->type = 0xC;
    NETWORK_$ALLOWED_SERVICE = 0;
    NETWORK_$PROCESS_PAGING_REQUEST(&ps);
    ASSERT_EQ(1, n_send);
    ASSERT_EQ(0xD, reply_type[0]);
    ASSERT_EQ(0x0011000F, reply_status[0]);
    ASSERT_EQ(0x4A, reply_len[0]);
    ASSERT_EQ(0, reply_page[0]);
}

static void test_pagin_two_pages(void)
{
    rqst->pagin.type = 0xC;
    rqst->pagin.version = 8;
    rqst->pagin.limit = 0x20;
    rqst->pagin.count = 2;
    rqst->pagin.page_size = 0x400;
    rqst->pagin.req.uid.high = 0x1111;
    rqst->pagin.req.page_num = 0x41;
    touch_ret = 2;
    rcv_stub.flags_hi = 1;                     /* queue depth 3 in bits 15..22 */
    rcv_stub.flags_lo = 0x8000;
    NETLOG_$OK_TO_LOG_SERVER = (int8_t)0xFF;
    NETWORK_$PROCESS_PAGING_REQUEST(&ps);
    ASSERT_EQ(2, n_wire);
    ASSERT_EQ(2, n_unwire);
    ASSERT_EQ(0, aste.wire_count);             /* activated and released */
    ASSERT_EQ(2, n_send);
    ASSERT_EQ(1, reply_seq[0]);
    ASSERT_EQ(2, reply_seq[1]);
    ASSERT_EQ(0x400u << 10, reply_page[0]);
    ASSERT_EQ(0x401u << 10, reply_page[1]);
    ASSERT_EQ(2, ps.reply.pagin.page_cnt);
    ASSERT_EQ(0x8000, (uint16_t)ps.reply.pagin.more);
    ASSERT_EQ(0xD7D7, ps.reply.pagin.dtm_high);
    ASSERT_EQ(2, NETWORK_$MULT_PAGIN_RQST_CNT);
    ASSERT_EQ(2, PROC1_$DATA.stats[2].stat[3]);
    ASSERT_EQ(1, n_log);
    ASSERT_EQ(2, log_args[0]);                 /* page 0x41 >> 5 */
    ASSERT_EQ(1, log_args[1]);                 /* page 0x41 & 0x1f */
    ASSERT_EQ(0x0131, log_args[2]);            /* kind 1, depth 3 << 4, node bits */
    ASSERT_EQ(0x2345, log_args[3]);
}

static void test_pagin_page_null_sends_one(void)
{
    rqst->pagin.type = 0xC;
    rqst->pagin.version = 1;
    rqst->pagin.limit = 0x20;
    rqst->pagin.count = 3;
    touch_ret = 3;
    touch_status = status_$pmap_page_null;
    NETWORK_$PROCESS_PAGING_REQUEST(&ps);
    ASSERT_EQ(0, n_wire);
    ASSERT_EQ(1, n_send);                      /* quirk: stops on the local status */
    ASSERT_EQ(NETWORK_$ZERO_PAGE_PA, reply_page[0]);
    ASSERT_EQ(3, ps.reply.pagin.page_cnt);
    ASSERT_EQ(0, n_unwire);
}

static void test_pagout_whole_page(void)
{
    rqst->pagout.type = 4;
    rqst->pagout.req.uid.high = 0x1111;
    rqst->pagout.version = 4;
    rcv_stub.data_pages[0] = 0x300u << 10;
    MMAPE_FOR_VPN(0x300)->wire_count = 2;
    NETWORK_$PROCESS_PAGING_REQUEST(&ps);
    ASSERT_EQ(1, NETWORK_$CLEAR_WIRED);
    ASSERT_EQ(0, MMAPE_FOR_VPN(0x300)->wire_count);
    ASSERT_EQ(1, n_assoc);
    ASSERT_EQ(0x777u << 10, rtn_dat_pa);       /* a fresh page for NETBUF */
    ASSERT_EQ(1, n_dts);
    ASSERT_EQ(5, reply_type[0]);
    ASSERT_EQ(0x34, reply_len[0]);
    ASSERT_EQ(8, ps.reply.pagout.version);
    ASSERT_EQ(0x1234, ps.reply.pagout.dtm_low);
    ASSERT_EQ(1, PROC1_$DATA.stats[2].stat[3]);
}

static void test_pagout_bad_checksum(void)
{
    rqst->pagout.type = 4;
    rqst->pagout.chksum = 0x1111;
    chksum_value = 0x2222;
    NETWORK_$DO_CHKSUM = (char)0xFF;
    rcv_stub.data_pages[0] = 0x300u << 10;
    NETWORK_$PROCESS_PAGING_REQUEST(&ps);
    ASSERT_EQ(1, n_crash);
    ASSERT_EQ(0x00110010, *crash_arg);
    ASSERT_EQ(1, n_dump);
    ASSERT_EQ(1, NETWORK_$BAD_CHKSUM_CNT);
    ASSERT_EQ(0, n_send);
}

static void test_setattr_too_late(void)
{
    rqst->setattr.type = 0xA;
    rqst->setattr.version = 4;
    rcv_stub.src_addr = 10;                    /* 10 + 0x14 < 100 */
    NETWORK_$PROCESS_PAGING_REQUEST(&ps);
    ASSERT_EQ(1, n_setattr);
    ASSERT_EQ(1, n_dts);
    ASSERT_EQ(0, n_send);
    ASSERT_EQ(1, NETWORK_$2LONG1);
    ASSERT_EQ(0x10, ps.reply.setattr.dtm_low); /* (0x1234 >> 8) & 0xf8 */
}

static void test_setattr_logged(void)
{
    rqst->setattr.type = 0xA;
    rqst->setattr.version = 8;
    rqst->setattr.uid.high = 0x4242;
    rcv_stub.src_addr = 100;
    NETLOG_$OK_TO_LOG_SERVER = (int8_t)0xFF;
    NETWORK_$PROCESS_PAGING_REQUEST(&ps);
    ASSERT_EQ(1, n_send);
    ASSERT_EQ(0xB, reply_type[0]);
    ASSERT_EQ(0xE, reply_len[0]);
    ASSERT_EQ(1, n_log);
    ASSERT_EQ(0x4242, log_uid_high);
    ASSERT_EQ(0, log_args[0]);
    ASSERT_EQ(0x0301, log_args[2]);
}

static void test_getattr_old_version(void)
{
    rqst->getattr.type = 6;
    rqst->getattr.version = 4;
    rcv->rqst_len = 0x10;
    NETWORK_$PROCESS_PAGING_REQUEST(&ps);
    ASSERT_EQ(1, n_new_to_old);
    ASSERT_EQ(7, reply_type[0]);
    ASSERT_EQ(0, reply_status[0]);
    ASSERT_EQ(0x48, reply_len[0]);
    ASSERT_EQ(8, ps.reply.getattr.u.old.version);
    ASSERT_EQ(0x5A5A5A5Au, ps.reply.getattr.u.old.attrs.l[0]);
}

static void test_getattr_long_form_with_uid_bits(void)
{
    rqst->getattr.type = 6;
    rqst->getattr.version = 8;
    rqst->getattr.uid.low = 0x02000000u;       /* bit 25 */
    NETWORK_$PROCESS_PAGING_REQUEST(&ps);
    ASSERT_EQ(0x000F0001, reply_status[0]);
    ASSERT_EQ(0xB8, reply_len[0]);
    ASSERT_EQ(0, n_new_to_old);
}

int main(void)
{
    printf("NETWORK_$PROCESS_PAGING_REQUEST tests:\n");
    RUN_TEST(receive_failure_does_nothing);
    RUN_TEST(unknown_type);
    RUN_TEST(echo);
    RUN_TEST(ring_info);
    RUN_TEST(pagin_denied);
    RUN_TEST(pagin_two_pages);
    RUN_TEST(pagin_page_null_sends_one);
    RUN_TEST(pagout_whole_page);
    RUN_TEST(pagout_bad_checksum);
    RUN_TEST(setattr_too_late);
    RUN_TEST(setattr_logged);
    RUN_TEST(getattr_old_version);
    RUN_TEST(getattr_long_form_with_uid_bits);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
