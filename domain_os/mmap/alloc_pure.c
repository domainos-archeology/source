/*
 * MMAP_$ALLOC_PURE - Allocate clean pages from the pure and impure pools
 *
 * Original address: 0x00E0D78E (226 bytes; `E0D78E MMAP_$ALLOC_PURE` in the
 * SAU2 map).  Single caller: 0x00E00D96.
 *
 * Frame (0x00E0D78E-0x00E0D7A0): `link.w A6,#-0x18`, D2-D7/A2-A5 saved,
 * A5 = the MMAP_ block (0xE23284).  Arguments, (0x8,A6) being argument 1:
 *   (0x8,A6)  vpn_array  longword array pointer (A4)
 *   (0xC,A6)  count      word (D2, counted down)
 *
 * Pascal function: the result is D0.w = pages allocated (D3).
 *
 * Registers: D3 = allocated so far, D4.b = "already trimmed once" boolean
 * (0 / `st' = 0xFF), D6 = this pool's share, A3/A2 = the pool cursor.
 *
 * 0x00E0D7A4            MMAP_$ALLOC_CNT++ (0x28,A5)
 * 0x00E0D7B2-0x00E0D7B6 (-0x18,A6) = A5 + 0x24, the cursor base.  Every
 *                        pool field is then read off that biased base:
 *                        (0x30,A2) = A5+0x54 = 0xE232D8 = MMAP_$WSL[1]
 *                        .page_count and `pea (0x2c,A2)' = &MMAP_$WSL[1].
 *                        The scan therefore starts at pool 1 (PURE) and the
 *                        `lea (0x24,A3),A3' / `dbf D5' (D5 = 1, two passes)
 *                        moves it to pool 2 (IMPURE).  The free pool is not
 *                        touched here - MMAP_$ALLOC_FREE owns it.
 * 0x00E0D7C2-0x00E0D7FC inner loop over pools 1..2: skip an empty pool;
 *                        D6 = min(count, page_count) (`clr.l D6 / move.w'
 *                        zero-extends, `cmp.l/bls'); take them with
 *                        mmap_$alloc_pages_from_wsl(&pool, &vpn_array[D3],
 *                        (word)D6); count -= D6; allocated += D6; count == 0
 *                        exits to done
 * 0x00E0D800-0x00E0D802 count == 0 -> done (only reachable when count
 *                        started at 0)
 * 0x00E0D804-0x00E0D808 allocated >= 8 (unsigned `bcc') -> done
 * 0x00E0D80A            MMAP_$STEAL_CNT++ (0x20,A5)
 * 0x00E0D80E-0x00E0D810 trimmed already -> done
 * 0x00E0D812-0x00E0D820 MMAP_$WSL[3].page_count (0x9C,A5) +
 *                        MMAP_$WSL[4].page_count (0xC0,A5) > 8 (unsigned
 *                        `bhi') -> done: too many dirty pages queued
 * 0x00E0D822-0x00E0D82E D4.w = MMAP_PID_TO_WSL[PROC1_$CURRENT]
 *                        ((0xA22,A5) + pid*2 = 0xE23CA6 + pid*2)
 * 0x00E0D832-0x00E0D844 MMAP_$WSL[D4].page_count < 0x180 (unsigned
 *                        `bcs', index * 0x24 built as *4 + *32) -> done
 * 0x00E0D846-0x00E0D854 mmap_$trim_wsl(D4, (long)count); a 2-byte Pascal
 *                        result slot is allocated and discarded (`addq.w
 *                        #8,SP' pops slot + long + word)
 * 0x00E0D856-0x00E0D858 `st D4b' (trimmed = true), back to the pool scan
 * 0x00E0D85C-0x00E0D864 done: MMAP_$ALLOC_PAGES (0x24,A5) += allocated;
 *                        result = allocated
 *
 * D4 does double duty in the image (boolean low byte, then the WSL index
 * word, then `st' on the low byte again); the two uses never overlap a
 * read, so they are two variables here.  No spin lock is taken anywhere in
 * this routine.
 */

#include "mmap/mmap_internal.h"
#include "proc1/proc1.h"

uint16_t MMAP_$ALLOC_PURE(uint32_t *vpn_array, uint16_t count)
{
    uint16_t allocated;   /* D3 */
    boolean trimmed;      /* D4.b */
    uint32_t share;       /* D6 */
    uint16_t wsl_index;   /* D4.w */
    uint16_t pool;

    MMAP_$ALLOC_CNT++;                                        /* 0x00E0D7A4 */
    allocated = 0;                                             /* 0x00E0D7A8 */
    trimmed = false;                                           /* 0x00E0D7AA */

    for (;;) {
        /* 0x00E0D7BE-0x00E0D7FC: pools 1 (pure) and 2 (impure), in order */
        for (pool = MMAP_WSL_POOL_PURE; pool <= MMAP_WSL_POOL_IMPURE; pool++) {
            ws_hdr_t *wsl = &MMAP_$WSL[pool];

            if (wsl->page_count == 0) {                        /* 0x00E0D7C4 */
                continue;
            }

            share = count;                                     /* 0x00E0D7CA */
            if (share > wsl->page_count) {                     /* 0x00E0D7CE */
                share = wsl->page_count;
            }

            mmap_$alloc_pages_from_wsl(wsl, &vpn_array[allocated],
                                       (uint16_t)share);       /* 0x00E0D7E8 */
            count -= (uint16_t)share;                          /* 0x00E0D7F0 */
            allocated += (uint16_t)share;                      /* 0x00E0D7F2 */
            if (count == 0) {                                  /* 0x00E0D7F4 */
                goto done;
            }
        }

        if (count == 0) {                                      /* 0x00E0D800 */
            goto done;
        }
        if (allocated >= 8) {                                  /* 0x00E0D804 */
            goto done;
        }

        MMAP_$STEAL_CNT++;                                     /* 0x00E0D80A */

        if (trimmed < 0) {                                     /* 0x00E0D80E */
            goto done;
        }
        if (MMAP_$WSL[MMAP_WSL_POOL_DIRTY_LOCAL].page_count
                + MMAP_$WSL[MMAP_WSL_POOL_DIRTY_RMT].page_count > 8) {
            goto done;                                         /* 0x00E0D812 */
        }

        wsl_index = MMAP_PID_TO_WSL[PROC1_$CURRENT];           /* 0x00E0D822 */
        if (MMAP_$WSL[wsl_index].page_count < 0x180) {         /* 0x00E0D83C */
            goto done;
        }

        mmap_$trim_wsl(wsl_index, (uint32_t)count);            /* 0x00E0D850 */
        trimmed = true;                                        /* 0x00E0D856 */
    }

done:
    MMAP_$ALLOC_PAGES += allocated;                            /* 0x00E0D860 */
    return allocated;                                          /* 0x00E0D864 */
}
