/*
 * pmap/test/test_fill_write_qblks.c - Unit tests for pmap_$fill_write_qblks
 * (0x00E1327E)
 *
 * The queue blocks live in an arena that ARCH_HOST_VA_BASE points at (the
 * chain link is a VA); the ASTE table, AOTEs, MMAPEs and segment map are
 * host arrays.
 */

#include <stdio.h>
#include <string.h>
#include <setjmp.h>

#include "pmap/pmap_internal.h"
#include "ast/ast.h"
#include "bat/bat.h"
#include "disk/disk.h"
#include "misc/misc.h"
#include "mmap/mmap.h"
#include "mmu/mmu.h"
#include "netlog/netlog.h"
#include "time/time.h"
#include "uid/uid.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  Running %s... ", #name); \
    test_##name(); \
    tests_passed++; \
    printf("PASSED\n"); \
} while (0)

#define ASSERT_EQ(expected, actual) do { \
    unsigned long _e = (unsigned long)(expected); \
    unsigned long _a = (unsigned long)(actual); \
    if (_e != _a) { \
        printf("FAILED\n    Expected: 0x%lx, Got: 0x%lx at line %d\n", \
               _e, _a, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

static aste_t aste_store[8];
static aote_t aote_store[8];
static mmape_t mmape_store[0x1000];
aste_t *ast_aste_base = aste_store;
mmape_t *mmap_mmape_base = mmape_store;
MODULE_DATA_DEFINE(pmap_$segmap_t, PMAP_$SEGMAP, 0x00ED5000);
uid_t ANON_$UID = { 0xA0A0, 0xB0B0 };
uint32_t TIME_$CURRENT_CLOCKH = 0x55667788;
int8_t NETLOG_$OK_TO_LOG;
uint8_t DISK_$DATA[DISK_$DATA_SIZE];

static int locks, unlocks, allocs, logs, removes;
static int16_t alloc_vol, alloc_n, alloc_reserved;
static uint32_t alloc_hint;
static uint32_t alloc_blocks[16];
static status_$t alloc_status;
static uint32_t last_remove;
static uint16_t log_args[6];
static jmp_buf crash_jmp;
static status_$t crash_status;

void ML_$LOCK(int16_t id) { (void)id; locks++; }
void ML_$UNLOCK(int16_t id) { (void)id; unlocks++; }
void MMU_$REMOVE(uint32_t ppn) { removes++; last_remove = ppn; }
void CRASH_SYSTEM(const status_$t *s) { crash_status = *s; longjmp(crash_jmp, 1); }
void BAT_$ALLOCATE(int16_t vol_idx, uint32_t hint, int16_t alloc_count,
                   int16_t use_reserved, uint32_t *blocks, status_$t *status)
{
    int i;
    allocs++;
    alloc_vol = vol_idx; alloc_hint = hint; alloc_n = alloc_count; alloc_reserved = use_reserved;
    for (i = 0; i < alloc_count && i < 16; i++) blocks[i] = alloc_blocks[i];
    *status = alloc_status;
}
void NETLOG_$LOG_IT(uint16_t kind, uint32_t *uid, uint16_t a, uint16_t b,
                    uint16_t c, uint16_t d, uint16_t e, uint16_t f)
{
    (void)uid; (void)d; (void)e; (void)f;
    logs++; log_args[0] = kind; log_args[1] = a; log_args[2] = b; log_args[3] = c;
}

#include "../fill_write_qblks.c"

static uint8_t arena[0x400];
#define QB_VA(i) (0x100u + (i) * 0x40u)
static uint32_t *qb(int i) { return (uint32_t *)(arena + QB_VA(i)); }

static void reset(void)
{
    ARCH_HOST_VA_BASE = (uintptr_t)arena;
    memset(arena, 0, sizeof arena);
    memset(aste_store, 0, sizeof aste_store);
    memset(aote_store, 0, sizeof aote_store);
    memset(mmape_store, 0, sizeof mmape_store);
    memset(&PMAP_$SEGMAP, 0, sizeof PMAP_$SEGMAP);
    memset(DISK_$DATA, 0, sizeof DISK_$DATA);
    locks = unlocks = allocs = logs = removes = 0;
    alloc_status = 0;
    NETLOG_$OK_TO_LOG = 0;
    /* chain block 0 -> block 1 -> block 2 */
    qb(0)[0] = QB_VA(1);
    qb(1)[0] = QB_VA(2);
    /* segment 3 = ASTE_BASE[2], object AOTE aote_store[1] */
    aste_store[2].aote = &aote_store[1];
    aste_store[2].segment = 0x21;
    aste_store[2].fm_block = 0x7890;
    aote_store[1].uid.high = 0x11111111;
    aote_store[1].uid.low = 0x22222222;
    aote_store[1].sub_type = 2;
    aote_store[1].vol_index = 7;
    aote_store[1].dtm_high = 0xABCD1234;
    aote_store[1].unknown_24 = 0x00050000;
}

static void page(uint32_t vpn, uint16_t seg, uint8_t idx, uint32_t daddr)
{
    mmape_store[vpn].segment = seg;
    mmape_store[vpn].seg_offset = idx;
    mmape_store[vpn].disk_addr = daddr;
}

TEST(header_fields_for_a_page_with_an_address)
{
    int32_t pages[1] = { 0x300 };
    reset();
    page(0x300, 3, 4, 0xFFC00000u | 0x1234);
    qb(0)[7] = 0xDEADBEEF;
    pmap_$fill_write_qblks(pages, qb(0), 1);
    ASSERT_EQ(0, allocs);
    ASSERT_EQ(0x11111111, qb(0)[8]);
    ASSERT_EQ(0x22222222, qb(0)[9]);
    ASSERT_EQ((0x21u << 5) + 4, qb(0)[10]);
    ASSERT_EQ(0x55667788, qb(0)[11]);
    ASSERT_EQ(0x00020000, qb(0)[12]);      /* sub_type at byte +0x31 */
    ASSERT_EQ(0, qb(0)[13]);
    ASSERT_EQ(0, qb(0)[14]);
    ASSERT_EQ(0xDEADBE07, qb(0)[7]);       /* vol_index in op_flags */
    ASSERT_EQ(0x1234, qb(0)[1]);
    ASSERT_EQ(0x300, qb(0)[5]);
    ASSERT_EQ(0, logs);
    ASSERT_EQ(0, removes);
}

TEST(anonymous_page_uses_anon_uid)
{
    int32_t pages[1] = { 0x300 };
    reset();
    page(0x300, 3, 4, 0x1234);
    mmape_store[0x300].flags2 = 0x80;
    pmap_$fill_write_qblks(pages, qb(0), 1);
    ASSERT_EQ(0xA0A0, qb(0)[8]);
    ASSERT_EQ(0x1234, qb(0)[9]);           /* the word at aote+0x2a */
    ASSERT_EQ(0, qb(0)[12]);
    ASSERT_EQ(0x05, qb(0)[7] & 0xFF);      /* the word at aote+0x24 */
}

/* two pages of the same segment without addresses: one BAT call for both,
 * hint from the ASTE's fm_block, reserved pool, flags bit 13 (0x2000) set */
TEST(allocates_for_the_run_of_unaddressed_pages)
{
    int32_t pages[2] = { 0x300, 0x301 };
    reset();
    page(0x300, 3, 4, 0);
    page(0x301, 3, 5, 0);
    alloc_blocks[0] = 0x2000; alloc_blocks[1] = 0x2001;
    pmap_$fill_write_qblks(pages, qb(0), 2);
    ASSERT_EQ(1, allocs);
    ASSERT_EQ(7, alloc_vol);
    ASSERT_EQ(0x789, alloc_hint);
    ASSERT_EQ(2, alloc_n);
    ASSERT_EQ(1, alloc_reserved);
    ASSERT_EQ(0x2000, mmape_store[0x300].disk_addr);
    ASSERT_EQ(0x2001, mmape_store[0x301].disk_addr);
    ASSERT_EQ(0x2000, aste_store[2].flags);   /* bset.b #5 on the HIGH byte of the flags word */
    ASSERT_EQ(0x2000, qb(0)[1]);
    ASSERT_EQ(0x2001, qb(1)[1]);
    ASSERT_EQ(0x301, qb(1)[5]);
    ASSERT_EQ(1, locks);
    ASSERT_EQ(1, unlocks);
}

TEST(hint_from_earlier_neighbour_through_its_mmape)
{
    int32_t pages[1] = { 0x300 };
    uint32_t *row = (uint32_t *)PMAP_SEGMAP_ROW(3);
    reset();
    page(0x300, 3, 4, 0);
    page(0x250, 3, 2, 0x5555);
    row[2] = PMAP_SEGMAP_L_VALID | 0x250;  /* entry 3 empty, entry 2 valid */
    alloc_blocks[0] = 0x2000;
    pmap_$fill_write_qblks(pages, qb(0), 1);
    ASSERT_EQ(0x5555, alloc_hint);
    ASSERT_EQ(1, alloc_n);
}

TEST(hint_from_later_neighbour_entry_address)
{
    int32_t pages[1] = { 0x300 };
    uint32_t *row = (uint32_t *)PMAP_SEGMAP_ROW(3);
    reset();
    page(0x300, 3, 4, 0);
    row[6] = 0x00003333;                   /* not VALID: its own address */
    alloc_blocks[0] = 0x2000;
    pmap_$fill_write_qblks(pages, qb(0), 1);
    ASSERT_EQ(0x3333, alloc_hint);
}

TEST(allocation_failure_crashes)
{
    int32_t pages[1] = { 0x300 };
    reset();
    page(0x300, 3, 4, 0);
    alloc_status = 0x10002;
    if (setjmp(crash_jmp) == 0) {
        pmap_$fill_write_qblks(pages, qb(0), 1);
        ASSERT_EQ(1, 0);
    }
    ASSERT_EQ(0x10002, crash_status);
}

TEST(netlog_and_checksum_removal)
{
    int32_t pages[1] = { 0x300 };
    uint32_t *row = (uint32_t *)PMAP_SEGMAP_ROW(3);
    reset();
    page(0x300, 3, 4, 0x1234);
    row[4] = PMAP_SEGMAP_L_VALID | PMAP_SEGMAP_L_INSTALLED | 0x300;
    NETLOG_$OK_TO_LOG = -1;
    DISK_$DO_CHKSUM = -1;
    pmap_$fill_write_qblks(pages, qb(0), 1);
    ASSERT_EQ(1, logs);
    ASSERT_EQ(3, log_args[0]);
    ASSERT_EQ(0x21, log_args[1]);
    ASSERT_EQ(4, log_args[2]);
    ASSERT_EQ(0x300, log_args[3]);
    ASSERT_EQ(1, removes);
    ASSERT_EQ(0x300, last_remove);
    ASSERT_EQ(PMAP_SEGMAP_L_VALID | 0x300, row[4]);
}

TEST(zero_count_does_nothing)
{
    int32_t pages[1] = { 0x300 };
    reset();
    pmap_$fill_write_qblks(pages, qb(0), 0);
    ASSERT_EQ(0, qb(0)[5]);
}

int main(void)
{
    printf("test_fill_write_qblks:\n");
    RUN_TEST(header_fields_for_a_page_with_an_address);
    RUN_TEST(anonymous_page_uses_anon_uid);
    RUN_TEST(allocates_for_the_run_of_unaddressed_pages);
    RUN_TEST(hint_from_earlier_neighbour_through_its_mmape);
    RUN_TEST(hint_from_later_neighbour_entry_address);
    RUN_TEST(allocation_failure_crashes);
    RUN_TEST(netlog_and_checksum_removal);
    RUN_TEST(zero_count_does_nothing);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
