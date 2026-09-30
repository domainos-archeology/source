/*
 * ast/test/test_read_area_pages.c - Unit tests for ast_$read_area_pages
 *                                   (0x00E02AF6) and
 *                                   ast_$read_area_pages_network (0x00E02CA6)
 *
 * Both .c files are #included directly and driven through mocks.  Pins:
 *
 *   disk:    the head block's header (uid, page number), one block per
 *            frame (daddr & 0x3FFFFF, ppn), READ_MULTI(vol, TRUE, TRUE),
 *            bit 31 on failure, frames [pages_read..allocated) freed,
 *            PROC_STATS[pid*4+2] += pages_read;
 *   network: buffers returned per frame, the request record, page size
 *            from the block-size nibble, no-read-ahead from TOUCHED,
 *            unfilled frames taken back and freed, the null-page path,
 *            the clock/length updates, the NETLOG kinds, and
 *            PROC_STATS[pid*4+3] += pages_read on both exits.
 */

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define uid_t ast_uid_t

static int tests_passed = 0;
static int tests_failed = 0;
static int current_failed = 0;

static void reset_state(void);

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do {                                                   \
    printf("  Running %-48s", #name);                                         \
    current_failed = 0;                                                       \
    reset_state();                                                            \
    test_##name();                                                            \
    if (current_failed == 0) { tests_passed++; printf("PASSED\n"); }          \
} while (0)

#define ASSERT_EQ(expected, actual) do {                                      \
    if ((unsigned long long)(expected) != (unsigned long long)(actual)) {      \
        printf("FAILED\n    Expected: 0x%llx, Got: 0x%llx at line %d\n",       \
               (unsigned long long)(expected),                                \
               (unsigned long long)(actual), __LINE__);                       \
        tests_failed++; current_failed = 1;                                   \
        return;                                                               \
    }                                                                         \
} while (0)

#include "ast/read_area_pages.c"
#include "ast/read_area_pages_network.c"

#include "proc1/proc1.h"
MODULE_DATA_DEFINE(proc1_$data_t, PROC1_$DATA, 0x00E254E8);
uint16_t PROC1_$CURRENT;
int8_t   NETLOG_$OK_TO_LOG;

static aote_t test_aote;
static aste_t test_aste;
static disk_io_req_t qblks[8];

static int lock_calls, unlock_calls;
void ML_$LOCK(int16_t id)   { lock_calls++; (void)id; }
void ML_$UNLOCK(int16_t id) { unlock_calls++; (void)id; }

static int16_t alloc_count, alloc_min, alloc_result;
int16_t ast_$allocate_pages(int16_t count, int16_t min_count, uint32_t *ppn_array)
{
    int i;
    alloc_count = count; alloc_min = min_count;
    for (i = 0; i < alloc_result; i++) { ppn_array[i] = 0x300 + i; }
    return alloc_result;
}

static int16_t getq_count;
void DISK_$GET_QBLKS(int16_t count, uint32_t *head, uint32_t *tail)
{
    int i;
    getq_count = count;
    for (i = 0; i < 8; i++) { qblks[i].free_next = ARCH_PTR_TO_VA(&qblks[i + 1]); }
    *head = ARCH_PTR_TO_VA(&qblks[0]);
    *tail = ARCH_PTR_TO_VA(&qblks[count - 1]);
}
static int rtnq_calls; static int16_t rtnq_count;
void DISK_$RTN_QBLKS(int16_t count, uint32_t head, uint32_t tail) { (void)head; (void)tail; rtnq_calls++; rtnq_count = count; }

static uint16_t rm_vol; static int16_t rm_f1, rm_f2; static int16_t rm_pages; static status_$t rm_status;
void DISK_$READ_MULTI(uint16_t vol, int8_t f1, int8_t f2, uint32_t head, uint32_t tail,
                      int16_t *pages_read, status_$t *status)
{
    (void)head; (void)tail;
    rm_vol = vol; rm_f1 = f1; rm_f2 = f2;
    *pages_read = rm_pages; *status = rm_status;
}

#define MAX_FREE 8
static int free_calls; static uint32_t free_vpns[MAX_FREE];
void MMAP_$FREE(uint32_t vpn) { if (free_calls < MAX_FREE) free_vpns[free_calls] = vpn; free_calls++; }

/* network */
static int rtn_calls; static uint32_t rtn_addrs[MAX_FREE];
void NETBUF_$RTN_DAT(uint32_t addr) { if (rtn_calls < MAX_FREE) rtn_addrs[rtn_calls] = addr; rtn_calls++; }
static int get_calls; static uint32_t get_next;
void NETBUF_$GET_DAT(uint32_t *out) { get_calls++; *out = get_next; get_next += 0x400; }

static void *ra_net; static network_$page_request_t ra_req; static uint16_t ra_page_size;
static int16_t ra_count, ra_result; static int8_t ra_no_ra; static uint8_t ra_flags;
static clock_t ra_dtm, ra_clock, ra_acl; static status_$t ra_status; static int ra_zero_first;
int16_t NETWORK_$READ_AHEAD(void *net_info, void *uid, uint32_t *ppn_array,
                            uint16_t page_size, int16_t count, int8_t no_read_ahead,
                            uint8_t flags, clock_t *dtm, clock_t *clock, clock_t *acl_info,
                            status_$t *status)
{
    ra_net = net_info; ra_req = *(network_$page_request_t *)uid; ra_page_size = page_size;
    ra_count = count; ra_no_ra = no_read_ahead; ra_flags = flags;
    *dtm = ra_dtm; *clock = ra_clock; *acl_info = ra_acl; *status = ra_status;
    if (ra_zero_first) { ppn_array[0] = 0; }
    return ra_result;
}
static int zero_calls; static uint32_t zero_ppns[MAX_FREE];
void ZERO_PAGE(uint32_t ppn) { if (zero_calls < MAX_FREE) zero_ppns[zero_calls] = ppn; zero_calls++; }
static int clock_calls;
void TIME_$CLOCK(clock_t *c) { clock_calls++; c->high = 0x01020304; c->low = 0x0506; }
static int log_calls; static uint16_t log_kind, log_p3, log_p4, log_p7;
void NETLOG_$LOG_IT(uint16_t kind, uint32_t *uid, uint16_t p3, uint16_t p4,
                    uint16_t p5, uint16_t p6, uint16_t p7, uint16_t p8)
{
    (void)uid; (void)p5; (void)p6; (void)p8;
    log_calls++; log_kind = kind; log_p3 = p3; log_p4 = p4; log_p7 = p7;
}

static void reset_state(void)
{
    memset(PROC1_$DATA.stats, 0, sizeof(PROC1_$DATA.stats));
    PROC1_$CURRENT = 5;
    NETLOG_$OK_TO_LOG = 0;
    memset(&test_aote, 0, sizeof(test_aote));
    memset(&test_aste, 0, sizeof(test_aste));
    memset(qblks, 0, sizeof(qblks));
    /* the queue blocks are addressed by 32-bit VA: base the arena here */
    ARCH_HOST_VA_BASE = (uintptr_t)qblks - 0x1000;
    test_aste.aote = &test_aote;
    test_aste.segment = 3;
    test_aote.uid.high = 0xAAAA; test_aote.uid.low = 0xBBBB;
    test_aote.vol_index = 2;
    lock_calls = unlock_calls = 0;
    alloc_result = 3; alloc_count = alloc_min = 0;
    getq_count = 0; rtnq_calls = 0;
    rm_pages = 3; rm_status = status_$ok;
    free_calls = 0; memset(free_vpns, 0, sizeof(free_vpns));
    rtn_calls = 0; get_calls = 0; get_next = 0x700 << 10;
    memset(&ra_req, 0, sizeof(ra_req)); ra_result = 3; ra_status = status_$ok; ra_zero_first = 0;
    memset(&ra_dtm, 0, sizeof(ra_dtm)); memset(&ra_clock, 0, sizeof(ra_clock)); memset(&ra_acl, 0, sizeof(ra_acl));
    zero_calls = 0; clock_calls = 0; log_calls = 0;
}

TEST(disk_read_all_pages)
{
    uint32_t segmap[3] = { 0xC0001111, 0x00002222, 0x00403333 };
    uint32_t ppns[32];
    status_$t status = 0;
    int16_t r;

    r = ast_$read_area_pages(&test_aste, segmap, ppns, 4, 3, &status);

    ASSERT_EQ(3, r);
    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(3, alloc_count); ASSERT_EQ(1, alloc_min);
    ASSERT_EQ(3, getq_count);
    ASSERT_EQ(0xAAAA, qblks[0].header[0]); ASSERT_EQ(0xBBBB, qblks[0].header[1]);
    ASSERT_EQ(3 * 32 + 4, qblks[0].header[2]);
    ASSERT_EQ(0x1111, qblks[0].daddr); ASSERT_EQ(0x300, qblks[0].ppn);
    ASSERT_EQ(0x2222, qblks[1].daddr); ASSERT_EQ(0x301, qblks[1].ppn);
    ASSERT_EQ(0x3333, qblks[2].daddr); ASSERT_EQ(0x302, qblks[2].ppn);
    ASSERT_EQ(2, rm_vol); ASSERT_EQ((uint16_t)-1, (uint16_t)rm_f1); ASSERT_EQ((uint16_t)-1, (uint16_t)rm_f2);
    ASSERT_EQ(1, rtnq_calls); ASSERT_EQ(3, rtnq_count);
    ASSERT_EQ(0, free_calls);
    ASSERT_EQ(3, PROC1_$DATA.stats[5].stat[2]);
    ASSERT_EQ(1, unlock_calls); ASSERT_EQ(1, lock_calls);
}

TEST(disk_short_read_frees_rest)
{
    uint32_t segmap[3] = { 1, 2, 3 };
    uint32_t ppns[32];
    status_$t status = 0;
    int16_t r;

    rm_pages = 1; rm_status = 0x00070001;
    r = ast_$read_area_pages(&test_aste, segmap, ppns, 0, 3, &status);

    ASSERT_EQ(1, r);
    ASSERT_EQ(0x80070001u, (uint32_t)status);
    ASSERT_EQ(2, free_calls);
    ASSERT_EQ(0x301, free_vpns[0]); ASSERT_EQ(0x302, free_vpns[1]);
    ASSERT_EQ(1, PROC1_$DATA.stats[5].stat[2]);
}

TEST(network_read_with_reply_clocks)
{
    uint32_t segmap[3] = { 0xC0001111, 0xC0002222, 0x00003333 };
    uint32_t ppns[32];
    status_$t status = 0x77;
    int16_t r;

    test_aote.flags = AOTE_FLAG_TOUCHED;
    test_aote.obj_uid.high = 0x00030000;            /* nibble 3 -> 4KB */
    test_aote.length = 0x1000;
    ra_dtm.high = 0xD1; ra_dtm.low = 0xD2; ra_clock.high = 0xC1; ra_clock.low = 0xC2;
    ra_acl.high = 0xA1; ra_acl.low = 0xA2;
    NETLOG_$OK_TO_LOG = -1;

    r = ast_$read_area_pages_network(&test_aste, segmap, ppns, 4, 3, 0x42, &status);

    ASSERT_EQ(3, r);
    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(0, test_aote.flags);                     /* TOUCHED cleared */
    ASSERT_EQ(0xFF, (uint8_t)ra_no_ra);
    ASSERT_EQ(3, rtn_calls); ASSERT_EQ(0x300u << 10, rtn_addrs[0]);
    ASSERT_EQ((uintptr_t)&test_aote.obj_loc_net, (uintptr_t)ra_net);
    ASSERT_EQ(0xAAAA, ra_req.uid.high); ASSERT_EQ(3 * 32 + 4, ra_req.page_num);
    ASSERT_EQ(0x1000, ra_page_size);
    ASSERT_EQ(3, ra_count); ASSERT_EQ(0x42, ra_flags);
    ASSERT_EQ(0, get_calls); ASSERT_EQ(0, zero_calls);
    /* entries: bits 31..22 kept (andi.l #-0x400000), the rest := 1 */
    ASSERT_EQ(0xC0000001u, segmap[0]); ASSERT_EQ(0xC0000001u, segmap[1]);
    ASSERT_EQ(0x00000001u, segmap[2]);
    /* clocks from the reply */
    ASSERT_EQ(0xD1, test_aote.dtu_high); ASSERT_EQ(0xD2, test_aote.dtu_low);
    ASSERT_EQ(0xA1, test_aote.dtm_high); ASSERT_EQ(0xA2, test_aote.dtm_low);
    ASSERT_EQ(0xC1, test_aote.dta_high); ASSERT_EQ(0xC2, test_aote.dta_low);
    ASSERT_EQ(0, clock_calls);
    /* end offset (96+4+3-1)<<10 = 0x19800 >= 0x1000: extended */
    ASSERT_EQ(0x19800 + 0x400, test_aote.length);
    ASSERT_EQ(1, log_calls); ASSERT_EQ(9, log_kind); ASSERT_EQ(3, log_p3);
    ASSERT_EQ(4, log_p4); ASSERT_EQ(3, log_p7);
    ASSERT_EQ(3, PROC1_$DATA.stats[5].stat[3]);
    ASSERT_EQ(1, unlock_calls); ASSERT_EQ(1, lock_calls);
}

TEST(network_null_pages_and_local_clock)
{
    uint32_t segmap[2] = { 0x00001111, 0x00002222 };
    uint32_t ppns[32];
    status_$t status = 0;
    int16_t r;

    test_aote.length = 0x100000;                       /* not extended */
    ra_result = 2; ra_zero_first = 1; alloc_result = 2;
    NETLOG_$OK_TO_LOG = -1;

    r = ast_$read_area_pages_network(&test_aste, segmap, ppns, 0, 2, 0, &status);

    ASSERT_EQ(2, r);
    ASSERT_EQ(2, get_calls);
    ASSERT_EQ(0x700, ppns[0]); ASSERT_EQ(0x701, ppns[1]);
    ASSERT_EQ(2, zero_calls); ASSERT_EQ(0x700, zero_ppns[0]);
    ASSERT_EQ(0x00400001u, segmap[0]); ASSERT_EQ(0x00400001u, segmap[1]);
    ASSERT_EQ(1, clock_calls);
    /* not extending, dtm zero: DTU := local clock only */
    ASSERT_EQ(0x01020304, test_aote.dtu_high); ASSERT_EQ(0x0506, test_aote.dtu_low);
    ASSERT_EQ(0, test_aote.dtm_high); ASSERT_EQ(0, test_aote.dta_high);
    ASSERT_EQ(0x100000, test_aote.length);
    ASSERT_EQ(8, log_kind);
}

TEST(network_short_read_and_nothing_read)
{
    uint32_t segmap[3] = { 1, 2, 3 };
    uint32_t ppns[32];
    status_$t status = 0;
    int16_t r;

    ra_result = 1; ra_status = 0x000F0002;
    r = ast_$read_area_pages_network(&test_aste, segmap, ppns, 0, 3, 0, &status);
    ASSERT_EQ(1, r);
    ASSERT_EQ(status_$ok, status);                     /* cleared once pages arrived */
    ASSERT_EQ(2, get_calls); ASSERT_EQ(2, free_calls);
    ASSERT_EQ(0x700, free_vpns[0]); ASSERT_EQ(0x701, free_vpns[1]);
    ASSERT_EQ(1, PROC1_$DATA.stats[5].stat[3]);

    reset_state();
    ra_result = 0; ra_status = 0x000F0002;
    r = ast_$read_area_pages_network(&test_aste, segmap, ppns, 0, 3, 0, &status);
    ASSERT_EQ(0, r);
    ASSERT_EQ(0x000F0002, status);                     /* left as the callee set it */
    ASSERT_EQ(3, free_calls);
    ASSERT_EQ(1, lock_calls);
    ASSERT_EQ(0, PROC1_$DATA.stats[5].stat[3]);
}

int main(void)
{
    printf("test_read_area_pages (0x00E02AF6 / 0x00E02CA6)\n");

    RUN_TEST(disk_read_all_pages);
    RUN_TEST(disk_short_read_frees_rest);
    RUN_TEST(network_read_with_reply_clocks);
    RUN_TEST(network_null_pages_and_local_clock);
    RUN_TEST(network_short_read_and_nothing_read);

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
