/*
 * mst/test/test_touch.c - unit tests for MST_$TOUCH (0x00E0DD40)
 *
 * The MSTE pages (0xEF6400) are a host array reached through
 * ARCH_HOST_VA_BASE; every callee is mocked and records its arguments.
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

#include "mst/mst_internal.h"

/* MST_PAGE_TABLE_BASE is ARCH_PTR_TO_VA(MSTE_PAGES), a sau2.ld symbol past
 * OS_PAGE_END since source-o7s2 (docs/rfc-cold-start.md section 8c); this
 * test stands in for the link with the map's value, `EF6400 MSTE_PAGES'
 * (os/test/test_vm_tables.c checks the macro itself). */
#undef MST_PAGE_TABLE_BASE
#define MST_PAGE_TABLE_BASE 0x00EF6400u
#include "netlog/netlog.h"
#include "peb/peb.h"
#include "node/node.h"
#include "anon/anon.h"
#include "file/file.h"

/* ---- data ------------------------------------------------------------ */

uint16_t MST[MST_TABLE_ENTRIES];
uint16_t MST_ASID_BASE[MST_MAX_ASIDS];
uint16_t MST_$SEG_MEM_TOP;
uint16_t MST_$PRIVATE_A_SIZE;
uint16_t MST_$SEG_PRIVATE_B;
uint16_t MST_$SEG_GLOBAL_A;
uint16_t MST_$GLOBAL_A_SIZE;
uint16_t MST_$SEG_GLOBAL_B;
uint16_t PROC1_$AS_ID;
int8_t NETLOG_$OK_TO_LOG;
uid_t ANON_$UID;
uid_t OS_WIRED_$UID;
uint32_t NODE_$ME;
peb_globals_t PEB_$INFO;
ast_$aot_t AST_$AOT;

static uint8_t pte_pages[4 * 0x400] __attribute__((aligned(0x400)));

/* ---- mocks ----------------------------------------------------------- */

static int lock_calls, unlock_calls, inhibit_begin, inhibit_end;
static int16_t last_lock, last_unlock;
static int netlog_calls;
static uint16_t netlog_count, netlog_asid, netlog_page;
static int area_touch_calls;
static uint16_t area_arg2, area_arg3;
static int16_t area_arg4;
static status_$t area_status;
static int vtop_calls;
static uint32_t vtop_ppn;
static status_$t vtop_status;
static int wire_calls;
static uint32_t wire_ppn;
static int peb_touch_calls;
static uint8_t peb_touch_result;
static int activate_calls;
static aste_t *activate_result;
static status_$t activate_status;
static int ast_touch_calls;
static uint16_t ast_touch_result, ast_touch_flags, ast_touch_page, ast_touch_count;
static uint32_t ast_touch_mode;
static status_$t ast_touch_status;
static uint8_t ast_touch_wire_seen;
static int install_calls;
static uint16_t install_count;
static uint32_t install_va, install_flags;

void ML_$LOCK(int16_t id) { lock_calls++; last_lock = id; }
void ML_$UNLOCK(int16_t id) { unlock_calls++; last_unlock = id; }
void PROC1_$INHIBIT_BEGIN(void) { inhibit_begin++; }
void PROC1_$INHIBIT_END(void) { inhibit_end++; }

void NETLOG_$LOG_IT(uint16_t kind, uint32_t *uid, uint16_t p3, uint16_t p4,
                    uint16_t p5, uint16_t p6, uint16_t p7, uint16_t p8)
{
    (void)kind; (void)uid; (void)p3; (void)p7; (void)p8;
    netlog_calls++;
    netlog_page = p4;
    netlog_count = p5;
    netlog_asid = p6;
}

void AREA_$TOUCH(area_$handle_t *handle_ptr, uint16_t bste_idx,
                 uint16_t seg_idx, int16_t param_4, uint32_t *ppn_array,
                 status_$t *status_p)
{
    (void)handle_ptr;
    area_touch_calls++;
    area_arg2 = bste_idx;
    area_arg3 = seg_idx;
    area_arg4 = param_4;
    ppn_array[0] = 0x777;
    *status_p = area_status;
}

uint32_t MMU_$VTOP(uint32_t va, status_$t *status)
{
    (void)va;
    vtop_calls++;
    *status = vtop_status;
    return vtop_ppn;
}

void MMAP_$WIRE(uint32_t vpn) { wire_calls++; wire_ppn = vpn; }

uint8_t PEB_$TOUCH(uint32_t *addr)
{
    (void)addr;
    peb_touch_calls++;
    return peb_touch_result;
}

aste_t *AST_$MSTE_ACTIVATE_AND_WIRE(mste_t *mste, status_$t *status)
{
    (void)mste;
    activate_calls++;
    *status = activate_status;
    if (activate_result != NULL) {
        activate_result->wire_count++;
    }
    return activate_result;
}

uint16_t AST_$TOUCH(aste_t *aste, uint32_t mode, uint16_t page, uint16_t count,
                    uint32_t *ppn_array, status_$t *status, uint16_t flags)
{
    ast_touch_calls++;
    ast_touch_mode = mode;
    ast_touch_page = page;
    ast_touch_count = count;
    ast_touch_flags = flags;
    ast_touch_wire_seen = aste->wire_count;
    ppn_array[0] = 0x555;
    *status = ast_touch_status;
    return ast_touch_result;
}

/* Pascal frame unpacked as the asm reads it (mmu/mmu.h); the flags value
 * recorded is the asid word : prot word pair the image's code tests. */
void (MMU_$INSTALL_LIST)(uint32_t count_array_slot, uint32_t array_va_slot,
                         uint32_t va_asid_slot, uint32_t prot_slot)
{
    uint16_t count = ARCH_PASCAL_SLOT_WORD(count_array_slot);
    /* ppn_array (ARCH_PASCAL_SLOTS_LONG(count_array_slot, array_va_slot))
     * points at MST_$TOUCH's stack, which a 64-bit host cannot rebuild
     * from the 32-bit VA the Pascal frame carries; it is not inspected. */
    uint32_t va = ARCH_PASCAL_SLOTS_LONG(array_va_slot, va_asid_slot);
    uint32_t flags = ARCH_PASCAL_WORD_PAIR_SLOT(
        ARCH_PASCAL_SLOT_WORD2(va_asid_slot), ARCH_PASCAL_SLOT_WORD(prot_slot));
    install_calls++;
    install_count = count;
    install_va = va;
    install_flags = flags;
}

#include "../touch.c"

/* ---- helpers --------------------------------------------------------- */

/* VA 0x00012345: segment 2 (private A), page 8, slot 2, asid 3 */
#define TEST_VA      0x00012345u
#define TEST_ASID    3
#define TEST_SLOT    2

static mst_entry_t *mste_for(uint16_t table_page, uint16_t slot)
{
    return (mst_entry_t *)(void *)(pte_pages + (table_page - 1) * 0x400 +
                                   (slot & 0x3F) * 16);
}

static mst_entry_t *setup_private(void)
{
    MST_ASID_BASE[TEST_ASID] = 0x10;
    MST[0x10] = 1;
    return mste_for(1, TEST_SLOT);
}

static void reset_state(void)
{
    memset(MST, 0, sizeof(MST));
    memset(MST_ASID_BASE, 0, sizeof(MST_ASID_BASE));
    memset(pte_pages, 0, sizeof(pte_pages));
    memset(&AST_$AOT, 0, sizeof(AST_$AOT));
    memset(&PEB_$INFO, 0, sizeof(PEB_$INFO));
    MST_$SEG_MEM_TOP = 0x200;
    MST_$PRIVATE_A_SIZE = 0x138;
    MST_$SEG_PRIVATE_B = 0x198;
    MST_$SEG_GLOBAL_A = 0x138;
    MST_$GLOBAL_A_SIZE = 0x60;
    MST_$SEG_GLOBAL_B = 0x1A0;
    PROC1_$AS_ID = TEST_ASID;
    NETLOG_$OK_TO_LOG = 0;
    ANON_$UID.high = 0x00000011; ANON_$UID.low = 0;
    OS_WIRED_$UID.high = 0x00000022; OS_WIRED_$UID.low = 0x33;
    NODE_$ME = 0x1234;
    lock_calls = unlock_calls = inhibit_begin = inhibit_end = 0;
    last_lock = last_unlock = 0;
    netlog_calls = 0;
    area_touch_calls = 0; area_status = status_$ok;
    vtop_calls = 0; vtop_ppn = 0; vtop_status = status_$ok;
    wire_calls = 0; wire_ppn = 0;
    peb_touch_calls = 0; peb_touch_result = 0;
    activate_calls = 0; activate_result = NULL; activate_status = status_$ok;
    ast_touch_calls = 0; ast_touch_result = 0; ast_touch_status = status_$ok;
    install_calls = 0;
    ARCH_HOST_VA_BASE = (uintptr_t)pte_pages - (uintptr_t)MST_PAGE_TABLE_BASE;
}

/* ---- tests ----------------------------------------------------------- */

static void test_beyond_mem_top_is_illegal(void)
{
    status_$t st = 0;
    uint32_t r = MST_$TOUCH(0x200u << 15, &st, 0);
    ASSERT_EQ(0, r);
    ASSERT_EQ(status_$reference_to_illegal_address, st);
    ASSERT_EQ(0, lock_calls);
}

static void test_gap_below_global_b_is_illegal(void)
{
    status_$t st = 0;
    /* segment 0x19F: past private B's 8, past global A, below global B */
    uint32_t r = MST_$TOUCH(0x19Fu << 15, &st, 0);
    ASSERT_EQ(0, r);
    ASSERT_EQ(status_$reference_to_illegal_address, st);
}

static void test_no_table_page_is_illegal(void)
{
    status_$t st = 0;
    MST_ASID_BASE[TEST_ASID] = 0x10;           /* MST[0x10] stays 0 */
    ASSERT_EQ(0, MST_$TOUCH(TEST_VA, &st, 0));
    ASSERT_EQ(status_$reference_to_illegal_address, st);
}

static void test_empty_entry_is_illegal(void)
{
    status_$t st = 0;
    setup_private();
    ASSERT_EQ(0, MST_$TOUCH(TEST_VA, &st, 0));
    ASSERT_EQ(status_$reference_to_illegal_address, st);
}

static void test_cached_aste_touch_and_install(void)
{
    status_$t st = 0;
    mst_entry_t *m = setup_private();
    aste_t *aste = AST_ASTE_ENTRY(5);
    aote_t aote;
    uint32_t r;

    memset(&aote, 0, sizeof(aote));
    m->uid.high = 0x01000000; m->uid.low = 0x42;
    m->area_id = 9;
    /* ASTE 5, prot 0x1F (0x3E00), active with bit 11 */
    m->flags = (uint16_t)(0x8000 | 0x3E00 | 5);
    m->page_info = (uint8_t)(3 << 2);          /* 4 pages */
    aote.uid = m->uid;
    aste->aote = &aote;
    aste->segment = 9;
    aste->wire_count = 2;
    ast_touch_result = 4;
    NETLOG_$OK_TO_LOG = (int8_t)0xFF;

    r = MST_$TOUCH(TEST_VA, &st, 0);
    ASSERT_EQ(0x555, r);
    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(0, activate_calls);
    ASSERT_EQ(3, ast_touch_wire_seen);         /* bumped around AST_$TOUCH */
    ASSERT_EQ(2, aste->wire_count);
    ASSERT_EQ(0x1234, ast_touch_mode);
    ASSERT_EQ(8, ast_touch_page);
    ASSERT_EQ(4, ast_touch_count);
    ASSERT_EQ(0x0001, ast_touch_flags);        /* private, active+bit11, bit9 set */
    ASSERT_EQ(4, install_count);
    ASSERT_EQ(TEST_VA, install_va);
    ASSERT_EQ(((uint32_t)TEST_ASID << 16) | 0x1F, install_flags);
    ASSERT_EQ(0, wire_calls);
    ASSERT_EQ(1, netlog_calls);
    ASSERT_EQ(4, netlog_count);
    ASSERT_EQ(TEST_ASID, netlog_asid);
    ASSERT_EQ(1, inhibit_begin);
    ASSERT_EQ(1, inhibit_end);
    ASSERT_EQ(2, lock_calls);                  /* 0x12 then 0x14 */
    ASSERT_EQ(MST_LOCK_MMU, last_unlock);
}

static void test_guard_fault_after_install(void)
{
    status_$t st = 0;
    mst_entry_t *m = setup_private();

    m->uid.high = 0x01000000;
    m->flags = (uint16_t)(0x4000 | 0x0200);    /* guard, no cached ASTE */
    activate_result = AST_ASTE_ENTRY(7);
    AST_ASTE_ENTRY(7)->seg_index = 7;
    ast_touch_result = 1;

    ASSERT_EQ(0x555, MST_$TOUCH(TEST_VA, &st, 1));
    ASSERT_EQ(status_$mst_guard_fault, st);
    ASSERT_EQ(0x0207, m->flags);               /* guard cleared, ASTE 7 cached */
    ASSERT_EQ(1, activate_calls);
    ASSERT_EQ(0x0000, ast_touch_flags);        /* bit 9 set, not active */
    ASSERT_EQ(1, wire_calls);
    ASSERT_EQ(0x555, wire_ppn);
}

static void test_global_segment_flags(void)
{
    status_$t st = 0;
    mst_entry_t *m;

    /* segment 0x138 is global A slot 0, asid 0 */
    MST_ASID_BASE[0] = 0x20;
    MST[0x20] = 2;
    m = mste_for(2, 0);
    m->uid.high = 0x01000000;
    m->flags = 0;
    activate_result = AST_ASTE_ENTRY(1);
    ast_touch_result = 1;

    MST_$TOUCH(0x138u << 15, &st, 0);
    ASSERT_EQ(0x0028, ast_touch_flags);        /* global + prot bit 0 clear */
    ASSERT_EQ(0, install_flags);               /* asid 0, prot 0 */
}

static void test_activate_failure_maps_status(void)
{
    status_$t st = 0;
    mst_entry_t *m = setup_private();

    m->uid.high = 0x01000000;
    activate_status = file_$object_not_found;
    ASSERT_EQ(0, MST_$TOUCH(TEST_VA, &st, 0));
    ASSERT_EQ(status_$mst_object_not_found, st);
    ASSERT_EQ(MST_LOCK_AST, last_unlock);
    ASSERT_EQ(1, inhibit_end);
    ASSERT_EQ(0, ast_touch_calls);
}

static void test_ast_touch_eof_maps_status(void)
{
    status_$t st = 0;
    mst_entry_t *m = setup_private();

    m->uid.high = 0x01000000;
    activate_result = AST_ASTE_ENTRY(2);
    ast_touch_result = 0;
    ast_touch_status = status_$ast_eof;
    ASSERT_EQ(0, MST_$TOUCH(TEST_VA, &st, 0));
    ASSERT_EQ(status_$mst_access_violation, st);
    ASSERT_EQ(MST_LOCK_MMU, last_unlock);
    ASSERT_EQ(0, install_calls);
}

static void test_other_failure_passes_through(void)
{
    status_$t st = 0;
    mst_entry_t *m = setup_private();

    m->uid.high = 0x01000000;
    activate_result = AST_ASTE_ENTRY(2);
    ast_touch_status = 0x00050004;
    ASSERT_EQ(0, MST_$TOUCH(TEST_VA, &st, 0));
    ASSERT_EQ(0x00050004, st);
}

static void test_anonymous_area_touch(void)
{
    status_$t st = 0;
    mst_entry_t *m = setup_private();

    m->uid = ANON_$UID;
    m->uid.low = 0x00010003;
    m->area_id = 6;
    m->flags = 0x0400;                         /* prot 2 */
    ASSERT_EQ(0x777, MST_$TOUCH(TEST_VA, &st, 0));
    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(1, area_touch_calls);
    ASSERT_EQ(6, area_arg2);
    ASSERT_EQ(8, area_arg3);
    ASSERT_EQ(TEST_SLOT, area_arg4);
    ASSERT_EQ(1, install_count);
    ASSERT_EQ(((uint32_t)TEST_ASID << 16) | 2, install_flags);
    ASSERT_EQ(0, lock_calls);                  /* never takes lock 0x14 ... */
    ASSERT_EQ(1, unlock_calls);                /* ... but releases it */
}

static void test_anonymous_area_failure(void)
{
    status_$t st = 0;
    mst_entry_t *m = setup_private();

    m->uid = ANON_$UID;
    area_status = status_$ast_eof;
    ASSERT_EQ(0, MST_$TOUCH(TEST_VA, &st, 0));
    ASSERT_EQ(status_$mst_access_violation, st);
    ASSERT_EQ(MST_LOCK_MMU, last_unlock);
    ASSERT_EQ(0, install_calls);
}

static void test_anonymous_global_is_illegal(void)
{
    status_$t st = 0;
    mst_entry_t *m;

    MST_ASID_BASE[0] = 0x20;
    MST[0x20] = 2;
    m = mste_for(2, 0);
    m->uid = ANON_$UID;
    ASSERT_EQ(0, MST_$TOUCH(0x138u << 15, &st, 0));
    ASSERT_EQ(status_$reference_to_illegal_address, st);
    ASSERT_EQ(0, area_touch_calls);
}

static void test_wired_vtop_path(void)
{
    status_$t st = 0;
    mst_entry_t *m = setup_private();

    m->uid.high = 0x00000099;                  /* first byte 0, not anon */
    vtop_ppn = 0x321;
    ASSERT_EQ(0x321, MST_$TOUCH(TEST_VA, &st, 1));
    ASSERT_EQ(1, wire_calls);
    ASSERT_EQ(0x321, wire_ppn);
    ASSERT_EQ(MST_LOCK_MMU, last_lock);
    ASSERT_EQ(MST_LOCK_MMU, last_unlock);

    reset_state();
    m = setup_private();
    m->uid.high = 0x00000099;
    vtop_ppn = 0x321;
    vtop_status = 0x00050008;
    ASSERT_EQ(0x321, MST_$TOUCH(TEST_VA, &st, 1));  /* returned regardless */
    ASSERT_EQ(0, wire_calls);
    ASSERT_EQ(0x00050008, st);
}

static void test_os_wired_segment(void)
{
    status_$t st = 0x99;
    mst_entry_t *m = setup_private();

    m->uid = OS_WIRED_$UID;
    PEB_$INFO.wcs_loaded = (int8_t)0xFF;
    peb_touch_result = 0xFF;
    ASSERT_EQ(TEST_SLOT, MST_$TOUCH(TEST_VA, &st, 0)); /* the MST slot */
    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(1, peb_touch_calls);

    peb_touch_result = 0;
    ASSERT_EQ(0, MST_$TOUCH(TEST_VA, &st, 0));
    ASSERT_EQ(status_$reference_to_illegal_address, st);

    PEB_$INFO.wcs_loaded = 0;
    peb_touch_calls = 0;
    ASSERT_EQ(0, MST_$TOUCH(TEST_VA, &st, 0));
    ASSERT_EQ(0, peb_touch_calls);
}

static void test_stale_cached_aste_is_reactivated(void)
{
    status_$t st = 0;
    mst_entry_t *m = setup_private();
    aste_t *aste = AST_ASTE_ENTRY(5);

    m->uid.high = 0x01000000;
    m->area_id = 9;
    m->flags = 5;
    aste->segment = 8;                         /* mismatch */
    activate_result = AST_ASTE_ENTRY(6);
    AST_ASTE_ENTRY(6)->seg_index = 6;
    ast_touch_result = 1;
    MST_$TOUCH(TEST_VA, &st, 0);
    ASSERT_EQ(1, activate_calls);
    ASSERT_EQ(6, m->flags & MSTE_FLAG_AST_MASK);
}

int main(void)
{
    printf("MST_$TOUCH tests:\n");
    RUN_TEST(beyond_mem_top_is_illegal);
    RUN_TEST(gap_below_global_b_is_illegal);
    RUN_TEST(no_table_page_is_illegal);
    RUN_TEST(empty_entry_is_illegal);
    RUN_TEST(cached_aste_touch_and_install);
    RUN_TEST(guard_fault_after_install);
    RUN_TEST(global_segment_flags);
    RUN_TEST(activate_failure_maps_status);
    RUN_TEST(ast_touch_eof_maps_status);
    RUN_TEST(other_failure_passes_through);
    RUN_TEST(anonymous_area_touch);
    RUN_TEST(anonymous_area_failure);
    RUN_TEST(anonymous_global_is_illegal);
    RUN_TEST(wired_vtop_path);
    RUN_TEST(os_wired_segment);
    RUN_TEST(stale_cached_aste_is_reactivated);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
