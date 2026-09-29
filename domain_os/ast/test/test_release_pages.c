/*
 * ast/test/test_release_pages.c - Unit tests for AST_$RELEASE_PAGES
 *                                 (0x00E06F88) and
 *                                 AST_$REMOVE_CORRUPTED_PAGE (0x00E07276)
 *
 * Both .c files are #included directly and driven through mocks.  Pins:
 *
 *   RELEASE_PAGES: the row is 1-based (base + seg*0x80 - 0x80); only
 *   entries with bits 30 AND 29 count; an unwired MMAPE is collected
 *   (bit 29 cleared) for one MMU_$REMOVE_LIST, a wired one is unmapped
 *   at once; MMAP_$RELEASE_PAGES only for a TRUE flag.
 *
 *   REMOVE_CORRUPTED_PAGE: the lock / range / WSL / segment / transition
 *   / installed gates; a clean page is invalidated (TRUE), a modified one
 *   (PFT or MMAPE) has its object's UID saved (FALSE).
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

#define TEST_N_FRAMES 0x1000
/* The SAU2 page frame table (SAU2_PFT_BASE, arch/m68k/sau2/hw.h) as the
 * test's own array; the code under test reaches it through PFT_BASE. */
static uint32_t       test_pft[TEST_N_FRAMES];
#define SAU2_PFT_BASE test_pft

#include "ast/release_pages.c"
#include "ast/remove_corrupted_page.c"

#define TEST_N_PAGES 32
#define TEST_N_ASTES 4
/* The AST_ module blocks (ast/ast.h) and the segment map (pmap/pmap.h). */
MODULE_DATA_DEFINE(ast_$data_t, AST_$DATA, 0x00E1DC80);
MODULE_DATA_DEFINE(ast_$aot_t, AST_$AOT, 0x00EC5400);
MODULE_DATA_DEFINE(pmap_$segmap_t, PMAP_$SEGMAP, 0x00ED5000);
MODULE_DATA_DEFINE(mmap_$mmape_table_t, MMAP_$MMAPE, 0x00EB4800);
uint16_t        PROC1_$CURRENT;

static aote_t test_aote;

static int lock_calls, unlock_calls;
void ML_$LOCK(int16_t id)   { lock_calls++; (void)id; }
void ML_$UNLOCK(int16_t id) { unlock_calls++; (void)id; }

#define MAX_REC 8
static int remove_calls; static uint32_t remove_ppns[MAX_REC];
void MMU_$REMOVE(uint32_t ppn) { if (remove_calls < MAX_REC) remove_ppns[remove_calls] = ppn; remove_calls++; }
static int list_calls; static uint16_t list_count; static uint32_t list_copy[32];
void MMU_$REMOVE_LIST(uint32_t *arr, uint16_t count) { list_calls++; list_count = count; memcpy(list_copy, arr, count * 4); }
static int release_calls; static uint16_t release_pid, release_count;
void MMAP_$RELEASE_PAGES(uint16_t pid, uint32_t *arr, uint16_t count) { (void)arr; release_calls++; release_pid = pid; release_count = count; }

static int8_t lock_held[0x20];
int8_t PROC1_$TST_LOCK(uint16_t id) { return lock_held[id]; }
static int inval_calls; static aste_t *inval_aste; static uint32_t *inval_entry; static uint32_t inval_ppn;
void AST_$INVALIDATE_PAGE(aste_t *aste, uint32_t *entry, uint32_t ppn) { inval_calls++; inval_aste = aste; inval_entry = entry; inval_ppn = ppn; }
static int save_calls; static uid_t *save_uid;
void AST_$SAVE_CLOBBERED_UID(uid_t *uid) { save_calls++; save_uid = uid; }

static uint32_t *row(int seg) { return (uint32_t *)PMAP_SEGMAP_ROW(seg); }

static void reset_state(void)
{
    memset(&PMAP_$SEGMAP, 0, sizeof(PMAP_$SEGMAP));
    memset(&MMAP_$MMAPE, 0, sizeof(MMAP_$MMAPE));
    memset(test_pft, 0, sizeof(test_pft));
    memset(&AST_$AOT, 0, sizeof(AST_$AOT));
    memset(&test_aote, 0, sizeof(test_aote));
    memset(lock_held, 0, sizeof(lock_held));
    PROC1_$CURRENT = 7;
    lock_calls = unlock_calls = 0;
    remove_calls = 0; list_calls = 0; release_calls = 0;
    inval_calls = 0; save_calls = 0;
}

TEST(release_collects_and_unmaps)
{
    aste_t aste;
    uint32_t *r = row(2);

    memset(&aste, 0, sizeof(aste));
    aste.seg_index = 2;
    r[0] = SEGMAP_VALID | SEGMAP_WIRED | 0x300;          /* unwired MMAPE */
    r[1] = SEGMAP_VALID | 0x301;                         /* not wired: skipped */
    r[2] = SEGMAP_VALID | SEGMAP_WIRED | 0x302;          /* MMAPE wired */
    MMAPE_FOR_VPN(0x302)->wire_count = 1;
    r[31] = SEGMAP_VALID | SEGMAP_WIRED | 0x303;
    row(3)[0] = SEGMAP_VALID | SEGMAP_WIRED | 0x304;     /* next row: untouched */

    AST_$RELEASE_PAGES(&aste, -1);

    ASSERT_EQ(SEGMAP_VALID | 0x300, r[0]);
    ASSERT_EQ(SEGMAP_VALID | 0x301, r[1]);
    ASSERT_EQ(SEGMAP_VALID | 0x302, r[2]);
    ASSERT_EQ(SEGMAP_VALID | 0x303, r[31]);
    ASSERT_EQ(SEGMAP_VALID | SEGMAP_WIRED | 0x304, row(3)[0]);
    ASSERT_EQ(1, remove_calls); ASSERT_EQ(0x302, remove_ppns[0]);
    ASSERT_EQ(1, list_calls); ASSERT_EQ(2, list_count);
    ASSERT_EQ(0x300, list_copy[0]); ASSERT_EQ(0x303, list_copy[1]);
    ASSERT_EQ(1, release_calls); ASSERT_EQ(7, release_pid); ASSERT_EQ(2, release_count);
    ASSERT_EQ(1, lock_calls); ASSERT_EQ(1, unlock_calls);
}

TEST(release_no_pool_and_nothing)
{
    aste_t aste;
    memset(&aste, 0, sizeof(aste));
    aste.seg_index = 1;
    row(1)[5] = SEGMAP_VALID | SEGMAP_WIRED | 0x310;

    AST_$RELEASE_PAGES(&aste, 0);
    ASSERT_EQ(1, list_calls);
    ASSERT_EQ(0, release_calls);

    reset_state();
    aste.seg_index = 1;
    AST_$RELEASE_PAGES(&aste, -1);
    ASSERT_EQ(0, list_calls);
    ASSERT_EQ(0, release_calls);
}

static void setup_corrupt(uint32_t ppn, int seg, int page)
{
    MMAPE_FOR_VPN(ppn)->flags1 = MMAPE_FLAG1_IN_WSL;
    MMAPE_FOR_VPN(ppn)->segment = (uint16_t)seg;
    MMAPE_FOR_VPN(ppn)->seg_offset = (uint8_t)page;
    row(seg)[page] = SEGMAP_VALID | ppn;
    AST_ASTE_ENTRY(seg)->aote = &test_aote;
}

TEST(corrupted_clean_page_invalidated)
{
    uint8_t r;

    setup_corrupt(0x350, 2, 9);
    r = AST_$REMOVE_CORRUPTED_PAGE(0x350);

    ASSERT_EQ(0xFF, r);
    ASSERT_EQ(1, inval_calls);
    ASSERT_EQ((uintptr_t)&AST_$AOT.aste[1], (uintptr_t)inval_aste);
    ASSERT_EQ((uintptr_t)&row(2)[9], (uintptr_t)inval_entry);
    ASSERT_EQ(0x350, inval_ppn);
    ASSERT_EQ(0, save_calls);
}

TEST(corrupted_modified_page_saved)
{
    uint8_t r;

    setup_corrupt(0x350, 2, 9);
    test_pft[0x350] = PFT_FLAG_MODIFIED;
    r = AST_$REMOVE_CORRUPTED_PAGE(0x350);
    ASSERT_EQ(0, r);
    ASSERT_EQ(0, inval_calls);
    ASSERT_EQ(1, save_calls);
    ASSERT_EQ((uintptr_t)&test_aote.uid, (uintptr_t)save_uid);

    reset_state();
    setup_corrupt(0x350, 2, 9);
    MMAPE_FOR_VPN(0x350)->flags2 = MMAPE_FLAG2_MODIFIED;
    r = AST_$REMOVE_CORRUPTED_PAGE(0x350);
    ASSERT_EQ(0, r);
    ASSERT_EQ(1, save_calls);
}

TEST(corrupted_gates)
{
    setup_corrupt(0x350, 2, 9);

    lock_held[AST_LOCK_ID] = -1;
    ASSERT_EQ(0, AST_$REMOVE_CORRUPTED_PAGE(0x350));
    lock_held[AST_LOCK_ID] = 0; lock_held[PMAP_LOCK_ID] = -1;
    ASSERT_EQ(0, AST_$REMOVE_CORRUPTED_PAGE(0x350));
    lock_held[PMAP_LOCK_ID] = 0;

    ASSERT_EQ(0, AST_$REMOVE_CORRUPTED_PAGE(0x1FF));
    ASSERT_EQ(0, AST_$REMOVE_CORRUPTED_PAGE(0x1000));

    MMAPE_FOR_VPN(0x350)->flags1 = 0;                      /* not in a WSL */
    ASSERT_EQ(0, AST_$REMOVE_CORRUPTED_PAGE(0x350));
    MMAPE_FOR_VPN(0x350)->flags1 = MMAPE_FLAG1_IN_WSL;

    MMAPE_FOR_VPN(0x350)->segment = 0;
    ASSERT_EQ(0, AST_$REMOVE_CORRUPTED_PAGE(0x350));
    MMAPE_FOR_VPN(0x350)->segment = 2;

    row(2)[9] |= SEGMAP_IN_TRANS;
    ASSERT_EQ(0, AST_$REMOVE_CORRUPTED_PAGE(0x350));
    row(2)[9] = 0x350;                                  /* not installed */
    ASSERT_EQ(0, AST_$REMOVE_CORRUPTED_PAGE(0x350));

    ASSERT_EQ(0, inval_calls); ASSERT_EQ(0, save_calls);
}

int main(void)
{
    printf("test_release_pages (AST_$RELEASE_PAGES 0x00E06F88, AST_$REMOVE_CORRUPTED_PAGE 0x00E07276)\n");

    RUN_TEST(release_collects_and_unmaps);
    RUN_TEST(release_no_pool_and_nothing);
    RUN_TEST(corrupted_clean_page_invalidated);
    RUN_TEST(corrupted_modified_page_saved);
    RUN_TEST(corrupted_gates);

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
