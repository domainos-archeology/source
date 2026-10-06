/*
 * pmap/test/test_write_page.c - unit tests for pmap_$write_page
 * (0x00E12E5E), linked with the real pmap_$write_complete (0x00E12D84)
 *
 * The MMAPE table, segment map, ASTE table, area table, PFT and PROC1
 * stats are host objects; NETWORK_$WRITE, DISK_$WRITE and the other
 * callees are mocked and record their arguments.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <setjmp.h>

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

#include "pmap/pmap_internal.h"
#include "area/area.h"
#include "ast/ast.h"
#include "ec/ec.h"
#include "file/file.h"
#include "misc/crash_system.h"

/* ---- data ------------------------------------------------------------ */

static uint32_t pft_store[0x1000];
#define SAU2_PFT_BASE pft_store   /* the SAU2 PFT (arch/m68k/sau2/hw.h) */
MODULE_DATA_DEFINE(pmap_$segmap_t, PMAP_$SEGMAP, 0x00ED5000);
MODULE_DATA_DEFINE(mmap_$mmape_table_t, MMAP_$MMAPE, 0x00EB4800);
MODULE_DATA_DEFINE(ast_$data_t, AST_$DATA, 0x00E1DC80);
MODULE_DATA_DEFINE(ast_$aot_t, AST_$AOT, 0x00EC5400);
proc1_$data_t PROC1_$DATA;
uint16_t PROC1_$CURRENT;
int8_t NETLOG_$OK_TO_LOG;
uint8_t DISK_$DATA[DISK_$DATA_SIZE];
area_$globals_t AREA_$GLOBALS;
uid_t ANON_$UID;
uint16_t NETWORK_$WRITE_VIOL_CNT;

static area_$entry_t mock_area_table[AREA_MAX_ENTRIES];
#undef AREA_TABLE_BASE
#define AREA_TABLE_BASE ((uintptr_t)mock_area_table)
#undef AREA_ENTRY_SIZE
#define AREA_ENTRY_SIZE ((int)sizeof(area_$entry_t))

/* ---- mocks ----------------------------------------------------------- */

static int locks, unlocks, removes, netlogs, crashes, clobbers, invalidates;
static int avails, advances, net_writes, disk_writes;
static uint32_t remove_vpn, crash_status;
static uint16_t netlog_kind, netlog_p3, netlog_p4, netlog_p5;
static uint32_t netlog_uid[2];
static uid_t net_node, net_uid;
static uint32_t net_page, net_word0c, net_word10, net_ppn;
static uint16_t net_pkt, net_count;
static status_$t net_status;
static int lock_depth_at_net, lock_depth_at_disk;
static int16_t disk_vol;
static uint32_t disk_daddr, disk_ppn;
static status_$t disk_status;
static uid_t *clobber_uid;
static uint32_t *inval_entry;
static aste_t *inval_aste;
static jmp_buf crash_jmp;
static int crash_jumps;

void ML_$LOCK(int16_t id) { (void)id; locks++; }
void ML_$UNLOCK(int16_t id) { (void)id; unlocks++; }
void MMU_$REMOVE(uint32_t ppn) { removes++; remove_vpn = ppn; }
void MMAP_$AVAIL(uint32_t vpn) { (void)vpn; avails++; }
void EC_$ADVANCE(ec_$eventcount_t *ec) { (void)ec; advances++; }

void NETLOG_$LOG_IT(uint16_t kind, uint32_t *uid, uint16_t p3, uint16_t p4,
                    uint16_t p5, uint16_t p6, uint16_t p7, uint16_t p8)
{
    (void)p6; (void)p7; (void)p8;
    netlogs++;
    netlog_kind = kind;
    netlog_uid[0] = uid[0];
    netlog_uid[1] = uid[1];
    netlog_p3 = p3; netlog_p4 = p4; netlog_p5 = p5;
}

void CRASH_SYSTEM(const status_$t *status_p)
{
    crashes++;
    crash_status = (uint32_t)*status_p;
    if (crash_jumps) {
        longjmp(crash_jmp, 1);
    }
}

void NETWORK_$WRITE(void *node_addr, network_$page_request_t *req,
                    uint32_t ppn, uint16_t pkt_size, uint16_t count,
                    uint16_t *dtv_low, clock_t *dtm, status_$t *status)
{
    net_writes++;
    lock_depth_at_net = locks - unlocks;
    net_node = *(uid_t *)node_addr;
    net_uid = req->uid;
    net_page = req->page_num;
    net_word0c = req->reserved[0];
    net_word10 = req->reserved[1] & 0xFFFF0000u;
    net_ppn = ppn;
    net_pkt = pkt_size;
    net_count = count;
    *dtv_low = 0x7777;
    dtm->high = 0x11112222;
    dtm->low = 0x3333;
    *status = net_status;
}

void DISK_$WRITE(int16_t vol_idx, uint32_t daddr, uint32_t ppn, uint32_t *info,
                 status_$t *status)
{
    (void)info;
    disk_writes++;
    lock_depth_at_disk = locks - unlocks;
    disk_vol = vol_idx;
    disk_daddr = daddr;
    disk_ppn = ppn;
    *status = disk_status;
}

void AST_$SAVE_CLOBBERED_UID(uid_t *uid) { clobbers++; clobber_uid = uid; }

void AST_$INVALIDATE_PAGE(aste_t *aste, uint32_t *segmap_entry, uint32_t ppn)
{
    (void)ppn;
    invalidates++;
    inval_aste = aste;
    inval_entry = segmap_entry;
}

#include "../write_complete.c"
#include "../write_page.c"

/* ---- fixture --------------------------------------------------------- */

#define VPN     0x345
#define SEG     7
#define PAGE    3
#define OBJSEG  2

static mmape_t *pg;
static uint32_t *ent;
static aste_t *as;
static aote_t the_aote;

static void reset_state(void)
{
    memset(&PMAP_$SEGMAP, 0, sizeof(PMAP_$SEGMAP));
    memset(&MMAP_$MMAPE, 0, sizeof(MMAP_$MMAPE));
    memset(&AST_$AOT, 0, sizeof(AST_$AOT));
    memset(&PROC1_$DATA, 0, sizeof(PROC1_$DATA));
    memset(DISK_$DATA, 0, sizeof(DISK_$DATA));
    memset(&AREA_$GLOBALS, 0, sizeof(AREA_$GLOBALS));
    memset(mock_area_table, 0, sizeof(mock_area_table));
    memset(&the_aote, 0, sizeof(the_aote));
    memset(pft_store, 0, sizeof(pft_store));
    PROC1_$CURRENT = 4;
    NETLOG_$OK_TO_LOG = 0;
    NETWORK_$WRITE_VIOL_CNT = 0;
    ANON_$UID.high = 0xAAAA0000;
    ANON_$UID.low = 0xBBBB;
    locks = unlocks = removes = netlogs = crashes = clobbers = 0;
    invalidates = avails = advances = net_writes = disk_writes = 0;
    crash_jumps = 0;
    net_status = disk_status = status_$ok;

    pg = MMAPE_FOR_VPN(VPN);
    pg->segment = SEG;
    pg->seg_offset = PAGE;
    pg->wsl_index = 3;
    ent = (uint32_t *)&PMAP_SEGMAP_ROW(SEG)[PAGE];
    *ent = 0x40000000u | VPN;
    as = AST_ASTE_ENTRY(SEG);
    as->aote = &the_aote;
    as->segment = OBJSEG;
    the_aote.uid.high = 0x12345678;
    the_aote.uid.low = 0x9ABCDEF0;
    the_aote.sub_type = 0x5A;
    the_aote.vol_index = 2;
}

/* ---- tests ----------------------------------------------------------- */

static void test_local_write(void)
{
    status_$t st = 0x99;
    pg->disk_addr = 0x00C01234;
    DISK_$DATA[DISK_$DO_CHKSUM_OFFSET] = 0xFF;
    *ent |= PMAP_SEGMAP_L_INSTALLED;
    NETLOG_$OK_TO_LOG = -1;

    pmap_$write_page(VPN, &st, 0);
    ASSERT_EQ(0, st);
    ASSERT_EQ(1, removes);                  /* checksumming uninstalls */
    ASSERT_EQ(0, *ent & PMAP_SEGMAP_L_INSTALLED);
    ASSERT_EQ(1, netlogs);
    ASSERT_EQ(3, netlog_kind);
    ASSERT_EQ(0x12345678, netlog_uid[0]);
    ASSERT_EQ((OBJSEG * 32 + PAGE) >> 5, netlog_p3);
    ASSERT_EQ(PAGE, netlog_p4);
    ASSERT_EQ(VPN, netlog_p5);
    ASSERT_EQ(1, disk_writes);
    ASSERT_EQ(0, lock_depth_at_disk + 1);   /* lock dropped around I/O */
    ASSERT_EQ(2, disk_vol);
    ASSERT_EQ(0x1234, disk_daddr);
    ASSERT_EQ(VPN, disk_ppn);
    ASSERT_EQ(1, PROC1_$DATA.stats[4].stat[2]);
    ASSERT_EQ(1, unlocks);
    ASSERT_EQ(1, locks);
    ASSERT_EQ(0, *ent & PMAP_SEGMAP_L_WRITING);
    ASSERT_EQ(1, pg->wsl_index);            /* write_complete ran */
    ASSERT_EQ(1, advances);
}

static void test_local_write_protected_is_success(void)
{
    status_$t st;
    pg->disk_addr = 0x10;
    disk_status = status_$disk_write_protected;
    pmap_$write_page(VPN, &st, 0);
    ASSERT_EQ(0, st);
    ASSERT_EQ(0, removes);                  /* checksums off */
    ASSERT_EQ(1, advances);
}

static void test_local_error_goes_to_complete(void)
{
    status_$t st;
    pg->disk_addr = 0x10;
    disk_status = 0x00080001;
    pmap_$write_page(VPN, &st, 0);
    ASSERT_EQ(0x80080001u, (uint32_t)st);
    ASSERT_EQ(1, avails);                   /* write_complete error arm */
    ASSERT_EQ(1, advances);                 /* only write_complete's */
}

static void test_local_no_disk_address_crashes(void)
{
    status_$t st;
    pg->disk_addr = 0x00C00000;
    pmap_$write_page(VPN, &st, 0);
    ASSERT_EQ(1, crashes);
    ASSERT_EQ(0x00050009, crash_status);
    ASSERT_EQ(1, disk_writes);              /* the image carries on */
}

static void test_remote_object_write(void)
{
    status_$t st;
    as->flags = ASTE_FLAG_REMOTE;
    the_aote.obj_loc_net = 0x00100000;
    the_aote.obj_loc_node = 0x0004321;
    the_aote.dtv_high = 0xD7D7;
    the_aote.obj_uid.high = 0x00010000;     /* packet 1 << 10 */
    the_aote.remote_flag = 0x01;            /* max 1 << 10 */
    the_aote.length = 0x100000;

    pmap_$write_page(VPN, &st, -1);
    ASSERT_EQ(0, st);
    ASSERT_EQ(1, net_writes);
    ASSERT_EQ(0, lock_depth_at_net + 1);
    ASSERT_EQ(0x00100000, net_node.high);
    ASSERT_EQ(0x0004321, net_node.low);
    ASSERT_EQ(0x12345678, net_uid.high);
    ASSERT_EQ(OBJSEG * 32 + PAGE, net_page);
    ASSERT_EQ(0xD7D7, net_word0c);
    ASSERT_EQ(0x005A0000, net_word10);
    ASSERT_EQ(0x400, net_pkt);
    ASSERT_EQ(1, net_count);
    ASSERT_EQ(0x7777, the_aote.dtv_low);
    ASSERT_EQ(0x11112222, the_aote.dtm_high);
    ASSERT_EQ(0x3333, the_aote.dtm_low);
    ASSERT_EQ(0x11112222, the_aote.dta_high);
    ASSERT_EQ(0x3333, the_aote.dta_low);
    ASSERT_EQ(1, PROC1_$DATA.stats[4].stat[3]);
    ASSERT_EQ(1, advances);
    ASSERT_EQ(0, removes);
}

static void test_remote_short_last_page(void)
{
    status_$t st;
    uint32_t off = (OBJSEG * 32 + PAGE) << 10;
    as->flags = ASTE_FLAG_REMOTE;
    the_aote.obj_uid.high = 0x00000000;     /* packet 1 << 9 */
    the_aote.remote_flag = 0x00;            /* max 0x200 < 0x400 */
    the_aote.length = off + 0x100;          /* ends in the page's last 0x200 */

    pmap_$write_page(VPN, &st, -1);
    ASSERT_EQ(0x400, net_pkt);
    ASSERT_EQ(1, net_count);                /* (0x100 + 0x3FF) div 0x400 */
    ASSERT_EQ(0, st);
}

static void test_remote_long_tail_keeps_packet(void)
{
    status_$t st;
    uint32_t off = (OBJSEG * 32 + PAGE) << 10;
    as->flags = ASTE_FLAG_REMOTE;
    the_aote.remote_flag = 0x00;            /* max 0x200 */
    the_aote.length = off + 0x300;          /* off + 0x200 < length: blt */

    pmap_$write_page(VPN, &st, -1);
    ASSERT_EQ(0x200, net_pkt);
    ASSERT_EQ(1, net_count);
}

static void test_remote_page_at_length_crashes(void)
{
    status_$t st;
    uint32_t off = (OBJSEG * 32 + PAGE) << 10;
    as->flags = ASTE_FLAG_REMOTE;
    the_aote.remote_flag = 0x00;
    the_aote.length = off;                  /* count 0x3FF div 0x400 = 0 */

    pmap_$write_page(VPN, &st, -1);
    ASSERT_EQ(1, crashes);
    ASSERT_EQ(0x00050006, crash_status);
    ASSERT_EQ(0, net_count);
    /* a synchronous write sent as anything but one packet: 0x3000C, then
     * write_complete's error arm sets bit 31 */
    ASSERT_EQ(0x8003000Cu, (uint32_t)st);
}

static void test_remote_page_beyond_length_keeps_packet(void)
{
    status_$t st;
    as->flags = ASTE_FLAG_REMOTE;
    the_aote.obj_uid.high = 0x00020000;     /* packet 1 << 11 */
    the_aote.remote_flag = 0x00;
    the_aote.length = 0x10;                 /* page offset is beyond it */
    pmap_$write_page(VPN, &st, -1);
    ASSERT_EQ(0x800, net_pkt);
    ASSERT_EQ(1, net_count);
}

static void test_remote_parity_error_saves_uid(void)
{
    status_$t st;
    as->flags = ASTE_FLAG_REMOTE;
    the_aote.remote_flag = 0x01;
    net_status = status_$network_memory_parity_error_during_transmit;
    pmap_$write_page(VPN, &st, 0);
    ASSERT_EQ(1, clobbers);
    ASSERT_EQ((unsigned long)&the_aote.uid, (unsigned long)clobber_uid);
    ASSERT_EQ(2, unlocks);
    ASSERT_EQ(2, locks);
    ASSERT_EQ(1, avails);                   /* then write_complete */
}

static void test_remote_concurrency_violation_invalidates(void)
{
    status_$t st;
    as->flags = ASTE_FLAG_REMOTE;
    the_aote.remote_flag = 0x01;
    net_status = status_$ast_write_concurrency_violation;
    pmap_$write_page(VPN, &st, 0);
    ASSERT_EQ(1, NETWORK_$WRITE_VIOL_CNT);
    ASSERT_EQ(1, invalidates);
    ASSERT_EQ((unsigned long)as, (unsigned long)inval_aste);
    ASSERT_EQ((unsigned long)ent, (unsigned long)inval_entry);
    ASSERT_EQ(0, *ent & PMAP_SEGMAP_L_WRITING);
    ASSERT_EQ(1, advances);
    ASSERT_EQ(0, avails);                   /* no write_complete */
    ASSERT_EQ(status_$ast_write_concurrency_violation, st);
}

static void test_remote_not_found_invalidates(void)
{
    status_$t st;
    as->flags = ASTE_FLAG_REMOTE;
    net_status = status_$file_object_not_found;
    pmap_$write_page(VPN, &st, 0);
    ASSERT_EQ(0, NETWORK_$WRITE_VIOL_CNT);
    ASSERT_EQ(1, invalidates);
}

static void test_area_page_remote_write(void)
{
    status_$t st;
    pg->flags2 = 0x80;
    as->flags = ASTE_FLAG_REMOTE;
    the_aote.dtm_high = 0x00000003;         /* area id 3 at +0x2A */
    mock_area_table[2].remote_volx = 0x0042;
    AREA_$GLOBALS.partner.high = 0x1;
    AREA_$GLOBALS.partner.low = 0x2;
    AREA_$GLOBALS.partner_pkt_size = 0x200;

    pmap_$write_page(VPN, &st, -1);
    ASSERT_EQ(0, crashes);
    ASSERT_EQ(0xAAAA0000, net_uid.high);
    ASSERT_EQ(0x42, net_uid.low);
    /* 0x43 + (0x43 + 0x100) >> 8 */
    ASSERT_EQ(0x44, net_page);
    ASSERT_EQ(0, net_word0c);
    ASSERT_EQ(0, net_word10);
    ASSERT_EQ(1, net_node.high);
    ASSERT_EQ(2, net_node.low);
    ASSERT_EQ(0x200, net_pkt);
    ASSERT_EQ(ASTE_FLAG_REMOTE | ASTE_FLAG_DIRTY, as->flags);
    ASSERT_EQ(0, st);
    ASSERT_EQ(1, PROC1_$DATA.stats[4].stat[3]);
    ASSERT_EQ(0, the_aote.dtv_low);
}

static void test_area_page_not_remote_crashes(void)
{
    status_$t st;
    pg->flags2 = 0x80;
    crash_jumps = 1;
    if (setjmp(crash_jmp) == 0) {
        pmap_$write_page(VPN, &st, 0);
    }
    ASSERT_EQ(1, crashes);
    ASSERT_EQ(0x0032000A, crash_status);
}

static void test_area_page_eof_crashes_with_status(void)
{
    status_$t st;
    pg->flags2 = 0x80;
    as->flags = ASTE_FLAG_REMOTE;
    net_status = status_$ast_eof;
    pmap_$write_page(VPN, &st, 0);
    ASSERT_EQ(1, crashes);
    ASSERT_EQ(status_$ast_eof, crash_status);
    ASSERT_EQ(0, invalidates);
}

int main(void)
{
    printf("pmap_$write_page tests:\n");
    RUN_TEST(local_write);
    RUN_TEST(local_write_protected_is_success);
    RUN_TEST(local_error_goes_to_complete);
    RUN_TEST(local_no_disk_address_crashes);
    RUN_TEST(remote_object_write);
    RUN_TEST(remote_short_last_page);
    RUN_TEST(remote_long_tail_keeps_packet);
    RUN_TEST(remote_page_at_length_crashes);
    RUN_TEST(remote_page_beyond_length_keeps_packet);
    RUN_TEST(remote_parity_error_saves_uid);
    RUN_TEST(remote_concurrency_violation_invalidates);
    RUN_TEST(remote_not_found_invalidates);
    RUN_TEST(area_page_remote_write);
    RUN_TEST(area_page_not_remote_crashes);
    RUN_TEST(area_page_eof_crashes_with_status);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
