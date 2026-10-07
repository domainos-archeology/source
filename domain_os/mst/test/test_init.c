/*
 * mst/test/test_init.c - unit tests for MST_$INIT (0x00E30B88)
 *
 * Bead source-ueq2.  MST_$INIT keeps its page cursor in three frame slots
 * that its nested procedure at 0x00E30B10 reaches through the static link:
 *
 *   (-0x10,A6)  the virtual address of the next page
 *   (-0x12,A6)  the bitmap LONGWORD index - `lsl.w #0x2` at 0x00E30B56,
 *               0x00E30B60, 0x00E30D66 and 0x00E30D90, `lsl.w #0x5` at
 *               0x00E30B34
 *   (-0x14,A6)  the BIT index - the operand of `bset.l D0,D1` (0x00E30B4E)
 *               and `bset.l D2,D1` (0x00E30D5E)
 *
 * 0x00E30C22 `clr.w (-0x12,A6)` and 0x00E30C26 `move.w #0x1,(-0x14,A6)`
 * therefore start the cursor at longword 0, bit 1 - bit 0 having just been
 * taken by `bclr.b #0x0,(0x3,A1)` at 0x00E30C10.  The tests below pin that
 * first bit (the first MST entry must read 1, not 32) and the store of the
 * running longword index into MST_$PAGE_ALLOC_HINT at 0x00E30D18.
 *
 * MST_$INIT dereferences target virtual addresses, so the test points
 * ARCH_HOST_VA_BASE at an arena that covers MST_PAGE_TABLE_BASE.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;

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
#include "math/math.h"
#include "misc/misc.h"

/* ------------------------------------------------------------------ */
/* The module data mst_data.c and mmu_data.c would supply.            */
/* ------------------------------------------------------------------ */

uint16_t MST_$TOUCH_COUNT;
uint16_t MST_$SEG_TN;
uint16_t MST_$GLOBAL_A_SIZE;
uint16_t MST_$GLOBAL_B_SIZE;
uint16_t MST_$MST_PAGES_WIRED;
uint16_t MST_$MST_PAGES_LIMIT;
uint8_t  MST_$ASID_LIST[8];
uint32_t MST_$PAGE_AVAIL_BITMAP[MST_$PAGE_AVAIL_BITMAP_LONGS];
uint16_t MST_$PAGE_ALLOC_HINT;
uint16_t MST[MST_TABLE_ENTRIES];
/*
 * MMAP_$PAGEABLE_PAGES and MMAP_$REAL_PAGES are two cells of the MMAP_
 * module data block (`D E23284 MMAP_ size = AA8'), so the test allocates
 * the block rather than the two scalars (bead source-mu8j).
 */
MODULE_DATA_DEFINE(mmap_globals_t, MMAP_$DATA, 0x00E23284);
MODULE_DATA_DEFINE(mmu_$globals_t, MMU_$GLOBALS, 0x00E23D2C);   /* M68020 (mmu/mmu.h) */

/* The four global-segment pages plus the MST pages land in this arena. */
static uint8_t page_arena[0x4000];

/* ------------------------------------------------------------------ */
/* Mocked callees                                                      */
/* ------------------------------------------------------------------ */

static int      mock_alloc_calls;
static int      mock_install_calls;
static int      mock_crash_calls;
static uint32_t mock_last_install_va;
static uint32_t mock_first_install_va;

uint16_t MMAP_$ALLOC_FREE(uint32_t *vpn_array, uint16_t count)
{
    (void)count;
    mock_alloc_calls++;
    vpn_array[0] = 0x1000u + (uint32_t)mock_alloc_calls;
    return 1;
}

void (MMU_$INSTALL)(uint32_t ppn, uint32_t va, uint32_t flags)
{
    (void)ppn;
    (void)flags;
    if (mock_install_calls == 0) {
        mock_first_install_va = va;
    }
    mock_install_calls++;
    mock_last_install_va = va;
}

void CRASH_SYSTEM(const status_$t *status_p)
{
    (void)status_p;
    mock_crash_calls++;
}

/* The three math routines MST_$INIT calls, in their host-arithmetic form. */
ulong M$MIU$LLW(ulong multiplicand, ushort multiplier)
{
    return (ulong)((uint32_t)multiplicand * (uint32_t)multiplier);
}

ulong M$DIU$LLW(ulong dividend, ushort divisor)
{
    return (ulong)((uint32_t)dividend / (uint32_t)divisor);
}

short M$OIS$WLW(long dividend, short divisor)
{
    return (short)((int32_t)dividend % (int32_t)divisor);
}

#include "mst/init.c"

/* ------------------------------------------------------------------ */

static void reset_state(void)
{
    int i;

    memset(page_arena, 0, sizeof(page_arena));
    memset(MST, 0xAA, sizeof(MST));
    memset(MST_$ASID_LIST, 0xAA, sizeof(MST_$ASID_LIST));
    for (i = 0; i < MST_$PAGE_AVAIL_BITMAP_LONGS; i++) {
        MST_$PAGE_AVAIL_BITMAP[i] = 0xFFFFFFFFu;   /* the image bytes */
    }
    MST_$PAGE_ALLOC_HINT = 0xDEAD;
    MST_$TOUCH_COUNT = 0;
    MST_$MST_PAGES_WIRED = 0xDEAD;
    MST_$MST_PAGES_LIMIT = 0xDEAD;

    /* One MST page (58 words) and four page-table pages fit the arena. */
    MST_$SEG_TN = 0x40;
    MST_$GLOBAL_A_SIZE = 0x80;      /* 2 pages */
    MST_$GLOBAL_B_SIZE = 0x80;      /* 2 more  */

    M68020 = 0;                 /* `tst.b`/`bmi` at 0x00E30C84 not taken */
    MMAP_$PAGEABLE_PAGES = 1000;
    MMAP_$REAL_PAGES = 1;

    mock_alloc_calls = 0;
    mock_install_calls = 0;
    mock_crash_calls = 0;
    mock_last_install_va = 0;
    mock_first_install_va = 0;

    ARCH_HOST_VA_BASE = (uintptr_t)page_arena - MST_PAGE_TABLE_BASE;
}

/*
 * The cursor starts at longword 0, bit 1, so the first page-table page the
 * global loop hands out is page 1 and the four entries run 1,2,3,4.  With
 * the two frame slots swapped they would run 32,33,34,35.
 */
static void test_first_bit_is_one_in_longword_zero(void)
{
    MST_$INIT();

    ASSERT_EQ(0, mock_crash_calls);
    ASSERT_EQ(1, MST[0]);                       /* 0x00E30B30..0x00E30B44 */
    ASSERT_EQ(2, MST[1]);
    ASSERT_EQ(3, MST[2]);
    ASSERT_EQ(4, MST[3]);
    ASSERT_EQ(4, MST_$MST_PAGES_WIRED);         /* 0x00E30B2C, four pages */

    /* bit 0 by the bclr at 0x00E30C10, bits 1..4 by the four bset/and */
    ASSERT_EQ(0xFFFFFFE0u, MST_$PAGE_AVAIL_BITMAP[0]);

    /* the cursor never left longword 0, so the hint stays there */
    ASSERT_EQ(0, MST_$PAGE_ALLOC_HINT);         /* 0x00E30D18 */
    /* mst_init_word and mst_init_bit are not checked here: the tail of the
     * body reuses both slots (0x00E30D32 and 0x00E30D50), so by the time
     * MST_$INIT returns they hold the bitmap-trimming cursor instead. */
}

/*
 * When a bitmap longword runs out (`tst.l` / `beq.b` at 0x00E30B62) the
 * cursor moves to the next longword and restarts at bit 0, and it is that
 * longword index - not the bit index - that 0x00E30D18 stores into
 * MST_$PAGE_ALLOC_HINT.
 */
static void test_page_alloc_hint_follows_the_longword_index(void)
{
    /* only pages 0 and 1 free, so the second global page rolls over */
    MST_$PAGE_AVAIL_BITMAP[0] = 0x00000003u;
    MST_$GLOBAL_A_SIZE = 0x40;      /* 1 page */
    MST_$GLOBAL_B_SIZE = 0x40;      /* 1 page */

    MST_$INIT();

    ASSERT_EQ(0, mock_crash_calls);
    ASSERT_EQ(1, MST[0]);                       /* longword 0, bit 1 */
    ASSERT_EQ(32, MST[1]);                      /* longword 1, bit 0 */
    ASSERT_EQ(0x00000000u, MST_$PAGE_AVAIL_BITMAP[0]);
    ASSERT_EQ(0xFFFFFFFEu, MST_$PAGE_AVAIL_BITMAP[1]);
    ASSERT_EQ(1, MST_$PAGE_ALLOC_HINT);         /* 0x00E30D18 */
}

/* The scalar stores the body makes on the way through. */
static void test_scalar_stores(void)
{
    MST_$INIT();

    ASSERT_EQ(4, MST_$TOUCH_COUNT);             /* 0x00E30B90 */

    /* 0x00E30B98 / 0x00E30B9E: two longwords leaving ASID 0 allocated */
    ASSERT_EQ(0, MST_$ASID_LIST[0]);
    ASSERT_EQ(0, MST_$ASID_LIST[3]);
    ASSERT_EQ(0, MST_$ASID_LIST[6]);
    ASSERT_EQ(1, MST_$ASID_LIST[7]);

    /* min((SEG_TN >> 6) * 58, (1000 * 10) / 100) = min(58, 100) = 58,
     * raised to the floor of 0x7d (0x00E30CE8) and rounded down to a
     * multiple of 32 by the asr/lsl pair at 0x00E30D10. */
    ASSERT_EQ(96, MST_$MST_PAGES_LIMIT);        /* 0x00E30D14 */

    /* 0x00E30D1E..0x00E30D70: page 97 is the first past the limit, so
     * longword 3 keeps only bit 0 ... */
    ASSERT_EQ(0x00000001u, MST_$PAGE_AVAIL_BITMAP[3]);
    /* ... and 0x00E30D74..0x00E30D9A clears longwords 4..11. */
    ASSERT_EQ(0u, MST_$PAGE_AVAIL_BITMAP[4]);
    ASSERT_EQ(0u, MST_$PAGE_AVAIL_BITMAP[11]);
    /* longwords 1 and 2 are left alone */
    ASSERT_EQ(0xFFFFFFFFu, MST_$PAGE_AVAIL_BITMAP[1]);
    ASSERT_EQ(0xFFFFFFFFu, MST_$PAGE_AVAIL_BITMAP[2]);
}

/*
 * The MST table pages come first (`move.l #0xee5800,(-0x10,A6)` at
 * 0x00E30BA8), then the page-table pages from 0xEF6400 (0x00E30C16), each
 * 0x400 further on (0x00E30B76).  Every page is zeroed by 0x00E30AF8.
 */
static void test_page_walk_and_zeroing(void)
{
    uint32_t i;

    MST_$INIT();

    ASSERT_EQ(5, mock_alloc_calls);             /* 1 MST page + 4 global */
    ASSERT_EQ(5, mock_install_calls);
    ASSERT_EQ(ARCH_PTR_TO_VA(MST), mock_first_install_va);
    ASSERT_EQ(MST_PAGE_TABLE_BASE + 0xC00u, mock_last_install_va);

    /* the four page-table pages were cleared, all 0x1000 bytes of them */
    for (i = 0; i < 0x1000u; i++) {
        if (page_arena[i] != 0) {
            ASSERT_EQ(0, page_arena[i]);
        }
    }

    /* the MST clear loop at 0x00E30C04 zeroed (uint16)(1 * 58) entries */
    ASSERT_EQ(0, MST[57]);
    ASSERT_EQ(0, MST[58]);      /* still inside the 0x400-byte page cleared */
}

int main(void)
{
    printf("Running MST_$INIT tests...\n\n");

    RUN_TEST(first_bit_is_one_in_longword_zero);
    RUN_TEST(page_alloc_hint_follows_the_longword_index);
    RUN_TEST(scalar_stores);
    RUN_TEST(page_walk_and_zeroing);

    printf("\n%d tests passed, %d tests failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
