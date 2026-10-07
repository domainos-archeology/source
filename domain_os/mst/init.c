/*
 * MST_$INIT (0x00E30B88) - full MST subsystem initialisation.
 *
 * Called once from OS_$INIT (0x00E33970).  It:
 *   1. sets the default touch-ahead count,
 *   2. seeds the ASID set with ASID 0 allocated,
 *   3. allocates and zeroes the pages backing the MST table at 0xEE5800,
 *   4. clears every MST word,
 *   5. allocates one page table page per Global A / Global B page and marks
 *      it used in the MST page availability bitmap,
 *   6. computes MST_$MST_PAGES_LIMIT from physical memory,
 *   7. marks every page above the limit unavailable in the bitmap.
 *
 * The body carries two nested Pascal procedures, 0x00E30AAE and 0x00E30B10.
 * The inner one reaches MST_$INIT's frame through the static link it loads
 * with `movea.l (A6),A2` (0x00E30B1C), so the three frame slots it shares
 * with its parent - (-0x10,A6), (-0x12,A6) and (-0x14,A6) - are modelled
 * here as file statics.
 *
 * Re-checked against the disassembly 2026-09-19 (0x00E30B88 .. 0x00E30DA7,
 * nested procedures 0x00E30AAE .. 0x00E30B09 and 0x00E30B10 .. 0x00E30B87):
 * no changes were needed.
 */

#include "mst/mst_internal.h"
#include "math/math.h"
#include "misc/misc.h"
#include "pmap/pmap.h"

/*
 * Status cells passed to CRASH_SYSTEM by `pea (d,PC)`.
 *
 * These are constant longwords in this module's own code region, not
 * shared globals; the cell address is part of each name.  Names come from
 * the SR10.4 status-code database.
 */
/* 0x00E30AD6: pea (0x34,PC) -> 0x00E30B0C, jsr CRASH_SYSTEM at 0x00E30ADA. */
static const status_$t mst_$vm_resources_exhausted_00e30b0c = 0x0004000E;

/*
 * MST_$INIT's shared frame slots, written by the parent and by the nested
 * procedure at 0x00E30B10 through its static link:
 *
 *   (-0x10,A6)  mst_init_va    longword virtual address of the next page
 *   (-0x12,A6)  mst_init_word  bitmap LONGWORD index; scaled `lsl.w #0x2`
 *                              at 0x00E30B56, 0x00E30B60, 0x00E30D66 and
 *                              0x00E30D90, and scaled `lsl.w #0x5` at
 *                              0x00E30B34 to form the page number
 *   (-0x14,A6)  mst_init_bit   bit index within that longword; the operand
 *                              of `bset.l D0,D1` at 0x00E30B4E and of
 *                              `bset.l D2,D1` at 0x00E30D5E
 */
static uint32_t mst_init_va;
static int16_t  mst_init_word;
static int16_t  mst_init_bit;

/*
 * Nested procedure at 0x00E30AAE - allocate a physical page, install it at
 * `va` and zero it.  Returns the physical page number (D0 at 0x00E30AFE).
 */
static uint32_t mst_init_page(uint32_t va)
{
    uint32_t ppn[3];        /* the 12 bytes at (-0xc,A6); `pea (-0xc,A6)` */
    uint16_t allocated;
    int16_t  i;
    uint32_t *p;

    /* 0x00E30ABE/0x00E30AC2/0x00E30AC6: MMAP_$ALLOC_FREE(&ppn, 1) */
    allocated = MMAP_$ALLOC_FREE(ppn, 1);

    /* 0x00E30ACE: the page number is read before the count is tested. */
    /* 0x00E30AD2 `tst.w D0w` / 0x00E30AD4 `bne.b` */
    if (allocated == 0) {
        CRASH_SYSTEM(&mst_$vm_resources_exhausted_00e30b0c);
    }

    /* 0x00E30AE2..0x00E30AEA: MMU_$INSTALL(ppn[0], va, 0x16) */
    MMU_$INSTALL(ppn[0], va, 0x16);

    /* 0x00E30AF4 `move.w #0xff,D0w` / `clr.l (A2)+` / `dbf`: 256 longwords */
    p = (uint32_t *)ARCH_VA_TO_PTR(va);
    for (i = 0xff; i >= 0; i--) {
        *p++ = 0;
    }

    return ppn[0];
}

/*
 * Nested procedure at 0x00E30B10 - back MST entry `seg_index` with a fresh
 * page table page and consume the next free bit of the availability bitmap.
 */
static void mst_init_global_page(int16_t seg_index)
{
    uint16_t page_num;

    /* 0x00E30B1E/0x00E30B22: the parent's (-0x10,A6) is the page address. */
    (void)mst_init_page(mst_init_va);

    /* 0x00E30B2C `addq.w #0x1,(0x34,A0)` with A0 = 0xE7CF0C */
    MST_$MST_PAGES_WIRED++;

    /* 0x00E30B30..0x00E30B44: MST[seg_index] = word*32 + bit */
    page_num = (uint16_t)((uint16_t)mst_init_word * 32u + (uint16_t)mst_init_bit);
    MST[seg_index] = page_num;

    /* 0x00E30B48..0x00E30B58: bitmap[word] &= ~(1 << bit) */
    MST_$PAGE_AVAIL_BITMAP[mst_init_word] &= ~(1u << (mst_init_bit & 0x1f));

    /* 0x00E30B5C `tst.l` / 0x00E30B66 `beq.b`: an exhausted longword moves
     * the cursor on to the next one, otherwise the bit index advances. */
    if (MST_$PAGE_AVAIL_BITMAP[mst_init_word] == 0) {
        mst_init_word++;        /* 0x00E30B6E */
        mst_init_bit = 0;       /* 0x00E30B72 */
    } else {
        mst_init_bit++;         /* 0x00E30B68 */
    }

    /* 0x00E30B76 `addi.l #0x400,(-0x10,A2)` */
    mst_init_va += 0x400;
}

void MST_$INIT(void)
{
    uint32_t mst_words;
    uint32_t ten_percent;
    uint32_t product;
    uint32_t page_base;
    uint32_t mst_base_va;
    uint32_t mst_end_va;
    int16_t  limit;
    int16_t  count;
    int16_t  seg;
    int16_t  bit;
    int16_t  next_word;
    uint16_t *mst_p;

    /* 0x00E30B90 `move.w #0x4,(0x00E24448).l` */
    MST_$TOUCH_COUNT = 4;

    /* 0x00E30B98 `clr.l (0x00E24384).l` and 0x00E30B9E
     * `move.l #0x1,(0x00E24388).l` write the eight ASID-set bytes as two
     * longwords; on the big-endian m68k the second store leaves
     * 00 00 00 01, i.e. bit 0 of the last byte, which is where
     * MST_$ALLOC_ASID looks for ASID 0 (reserved for the kernel). */
    MST_$ASID_LIST_STORE_LONG(0, 0);
    MST_$ASID_LIST_STORE_LONG(1, 1);

    /* 0x00E30BA8 `move.l #0xee5800,(-0x10,A6)` */
    mst_base_va = ARCH_PTR_TO_VA(MST);
    mst_init_va = mst_base_va;

    /* 0x00E30BB0..0x00E30BC8: M$MIU$LLW(((uint16)(SEG_TN + 0x3f)) >> 6, 58).
     * The word sum is zero-extended into the cleared D3 (0x00E30BB2,
     * 0x00E30BBE) and only then shifted right by six (0x00E30BC0), so the
     * multiplicand is the per-ASID page count and the product is the total
     * number of MST words. */
    mst_words = M$MIU$LLW((uint32_t)(uint16_t)(MST_$SEG_TN + 0x3f) >> 6,
                          MST_MAX_ASIDS);

    /* 0x00E30BD2..0x00E30BF6: allocate pages up to MST + mst_words*2.
     * `cmpa.l (-0x10,A6),A2` / `bhi.b` is an unsigned compare. */
    mst_end_va = mst_base_va + mst_words * 2;
    do {
        (void)mst_init_page(mst_init_va);
        mst_init_va += 0x400;
    } while (mst_end_va > mst_init_va);

    /* 0x00E30BF8..0x00E30C06: `clr.w (A0)+` / `dbf D0w` clears
     * (uint16)mst_words entries starting at MST. */
    count = (int16_t)((uint16_t)mst_words - 1);
    mst_p = MST;
    do {
        *mst_p++ = 0;
        count--;
    } while (count != -1);

    /* 0x00E30C10 `bclr.b #0x0,(0x3,A1)` with A1 = 0xE7CF0C: byte 0xE7CF0F is
     * the low byte of bitmap longword 0 on the big-endian m68k, so this
     * clears bit 0 - page 0 is never handed out. */
    MST_$PAGE_AVAIL_BITMAP[0] &= ~1u;

    /* 0x00E30C16 `move.l #0xef6400,(-0x10,A6)`: MSTE_PAGES, a run-time
     * window past OS_PAGE_END that COLD does not map (source-o7s2) */
    mst_init_va = MST_PAGE_TABLE_BASE;

    /* 0x00E30C1E `clr.w (0x34,A1)` */
    MST_$MST_PAGES_WIRED = 0;

    /* 0x00E30C22 `clr.w (-0x12,A6)` and 0x00E30C26 `move.w #0x1,(-0x14,A6)`:
     * the cursor starts at longword 0, bit 1 - bit 0 having just been taken
     * by the bclr above. */
    mst_init_word = 0;
    mst_init_bit = 1;

    /* 0x00E30C2C..0x00E30C48: one page per 64 Global A segments.
     * `subq.w #0x1,D0w` / `bmi.b` (0x00E30C36) is a SIGNED word test. */
    count = (int16_t)((MST_$GLOBAL_A_SIZE >> 6) - 1);
    if (count >= 0) {
        seg = 0;
        do {
            mst_init_global_page(seg);
            seg++;
            count--;
        } while (count != -1);
    }

    /* 0x00E30C4C..0x00E30C80: the same for Global B, whose entries follow
     * Global A's.  The sizes are added as zero-extended longwords
     * (0x00E30C52..0x00E30C64) before the shift. */
    seg = (int16_t)(MST_$GLOBAL_A_SIZE >> 6);
    count = (int16_t)(((((uint32_t)MST_$GLOBAL_A_SIZE +
                         (uint32_t)MST_$GLOBAL_B_SIZE) >> 6) - 1) - (uint32_t)seg);
    if (count >= 0) {
        do {
            mst_init_global_page(seg);
            seg++;
            count--;
        } while (count != -1);
    }

    /* 0x00E30C84 `tst.b (0x00e23d2e).l` / `bmi.b`: the sign of the M68020
     * flag selects which physical page count feeds the limit. */
    if (MST_M68020_IS_020()) {
        page_base = MMAP_$REAL_PAGES;       /* 0x00E30C94 */
    } else {
        page_base = MMAP_$PAGEABLE_PAGES;   /* 0x00E30C8C */
    }

    /* 0x00E30C9C..0x00E30CB2: 10 percent of those pages.  The multiply by
     * ten is open-coded as 2x + 8x. */
    ten_percent = M$DIU$LLW(page_base * 10, 100);

    /* 0x00E30CB8..0x00E30CD0: (SEG_TN >> 6) * 58 pages would back every ASID */
    product = M$MIU$LLW((uint32_t)MST_$SEG_TN >> 6, MST_MAX_ASIDS);

    /* 0x00E30CD2 `cmp.l (-0x20,A6),D0` / `bls.b`: take the smaller. */
    if (product > ten_percent) {
        product = ten_percent;
    }

    /* The limit is stored after every clamp, exactly as the original does. */
    MST_$MST_PAGES_LIMIT = (uint16_t)product;           /* 0x00E30CE2 */

    limit = 0x7d;                                       /* 0x00E30CE6 */
    if (limit < (int16_t)MST_$MST_PAGES_LIMIT) {        /* 0x00E30CE8 */
        limit = (int16_t)MST_$MST_PAGES_LIMIT;          /* 0x00E30CEE */
    }
    MST_$MST_PAGES_LIMIT = (uint16_t)limit;             /* 0x00E30CF4 */

    if (limit > MST_MSTE_PAGES_MAX) {                   /* 0x00E30CF8 */
        limit = MST_MSTE_PAGES_MAX;                     /* 0x00E30CFE */
    }
    MST_$MST_PAGES_LIMIT = (uint16_t)limit;             /* 0x00E30D04 */

    if (limit < 0) {                                    /* 0x00E30D08 */
        limit = (int16_t)(limit + 0x1f);                /* 0x00E30D0A */
    }
    /* 0x00E30D10 `asr.w #0x5` / 0x00E30D12 `lsl.w #0x5`: round to 32 */
    limit = (int16_t)((int16_t)(limit >> 5) << 5);
    MST_$MST_PAGES_LIMIT = (uint16_t)limit;             /* 0x00E30D14 */

    /* 0x00E30D18 `move.w (-0x12,A6),(0x30,A1)`: the bitmap longword index
     * the global-page loop left behind becomes the allocator's search hint.
     * It is a store of that running index, not a clear. */
    MST_$PAGE_ALLOC_HINT = (uint16_t)mst_init_word;

    /* 0x00E30D1E..0x00E30D32: the first page past the limit, as a longword,
     * split into a bitmap longword index (rounded toward zero) ... */
    {
        int32_t first_free = (int32_t)limit + 1;
        int32_t rounded = first_free;

        if (rounded < 0) {                              /* 0x00E30D26 */
            rounded += 0x1f;                            /* 0x00E30D28 */
        }
        mst_init_word = (int16_t)(rounded >> 5);        /* 0x00E30D32 */

        /* ... and a bit index within it (0x00E30D36: M$OIS$WLW(x, 32)).
         * 0x00E30D50 writes that bit index back to (-0x14,A6) only on the
         * path that runs the loop, so the store lives inside the guard. */
        bit = M$OIS$WLW(first_free, 32);                /* 0x00E30D46 */
    }

    /* 0x00E30D48..0x00E30D70: clear the remaining bits of that longword. */
    count = (int16_t)(0x1f - bit);                      /* 0x00E30D4A */
    if (count >= 0) {
        mst_init_bit = bit;                             /* 0x00E30D50 */
        do {
            MST_$PAGE_AVAIL_BITMAP[mst_init_word] &=
                ~(1u << (mst_init_bit & 0x1f));         /* 0x00E30D5E/D68 */
            mst_init_bit++;                             /* 0x00E30D6C */
            count--;
        } while (count != -1);
    }

    /* 0x00E30D74..0x00E30D9A: clear every bitmap longword above it, up to
     * and including index 11 (`moveq #0xb,D2` at 0x00E30D78).  The advanced
     * index is written back at 0x00E30D82, inside the guard. */
    next_word = (int16_t)(mst_init_word + 1);           /* 0x00E30D7A */
    count = (int16_t)(0xb - next_word);                 /* 0x00E30D7C */
    if (count >= 0) {
        mst_init_word = next_word;                      /* 0x00E30D82 */
        do {
            MST_$PAGE_AVAIL_BITMAP[mst_init_word] = 0;  /* 0x00E30D92 */
            mst_init_word++;                            /* 0x00E30D96 */
            count--;
        } while (count != -1);
    }
}
