/*
 * MMAP_$FREE_PAGES - Move an array of installed pages to the free pool
 *
 * Original address: 0x00E0CE56 (412 bytes; `E0CE56 MMAP_$FREE_PAGES` in the
 * SAU2 map).  Single caller: ast_$flush_installed_pages at 0x00E03FE4,
 * which pushes count, &array and then PROC1_$CURRENT (bead source-8yhy).
 *
 * Frame (0x00E0CE56-0x00E0CE68): `link.w A6,#-0x38`, D2-D6/A2-A5 saved,
 * A5 = the MMAP_ block (0xE23284).  Arguments, (0x8,A6) being argument 1:
 *   (0x8,A6)  pid        word - NEVER read by the callee
 *   (0xA,A6)  vpn_array  longword array pointer (A4)
 *   (0xE,A6)  count      word (D5)
 *
 * Locals: (-0x24,A6) = &MMAP_$WSL[0] (A5+0x2C); (-0x26,A6) = spin token;
 * (-0x1C,A6) = vpn_array[0]; (-0x38,A6) = count zero-extended;
 * (-0x18,A6) = vpn_array[count-1]; (-0x14,A6) = the array element after
 * the current one; (-0x10,A6) = the array element before it (0 at first).
 * The mmape_t array is 0xEB4800 + vpn*0x10 with -0x2000-based
 * displacements: -0x1FFC wsl_index (+4), -0x1FFB flags1 (+5), -0x1FFA
 * prev_vpn (+6), -0x1FF8 the priority/flags2 word (+8), -0x1FF6 next_vpn
 * (+0xA).
 *
 * 0x00E0CE74-0x00E0CE7E  token = ML_$SPIN_LOCK(&MMAP_GLOBALS.lock)
 * 0x00E0CE82-0x00E0CE9E  first = vpn_array[0]; last = vpn_array[count-1]
 *                        (read even when count == 0 - the image has no
 *                        guard); next = first; prev = 0
 * 0x00E0CEA2-0x00E0CEA4  count == 0 skips the unlink loop only
 * 0x00E0CEA8-0x00E0CF4A  `dbf' on count-1 = count iterations, D3 = 1-based
 *                        index, A2 = &vpn_array[1]:
 *   0x00E0CEB8-0x00E0CEC0  vpn = next; if D3 < count: next = *A2 (so on
 *                          the last element `next' stays equal to vpn)
 *   0x00E0CECC-0x00E0CEDC  flags1 bit 7 (IN_WSL) clear ->
 *                          CRASH_SYSTEM(&mmap_$inconsistent_mmape_00e0cff4)
 *   0x00E0CEDE-0x00E0CEF0  wsl = &MMAP_$WSL[page->wsl_index] (index * 0x24
 *                          built as *4 + *32, biased base A5 so (0x30,A0)
 *                          is page_count and (0x38,A0) head_vpn)
 *   0x00E0CEF4-0x00E0CF14  unlink: mmape[prev_vpn].next_vpn = next_vpn;
 *                          mmape[next_vpn].prev_vpn = prev_vpn
 *   0x00E0CF18-0x00E0CF24  if vpn == wsl->head_vpn: head_vpn = page->prev_vpn
 *   0x00E0CF28            wsl->page_count--
 *   0x00E0CF2C-0x00E0CF3C  page->prev_vpn = (word)next; page->next_vpn =
 *                          (word)prev; page->wsl_index = 0; `andi.w
 *                          #0xff3f' on the word at +8 = clear bits 7 and 6
 *                          of flags2 (the low byte; priority, the high byte, is kept)
 *   0x00E0CF42            prev = vpn
 * 0x00E0CF4E-0x00E0CF84  free pool empty: mmape[first].next_vpn = last;
 *                        mmape[last].prev_vpn = first; head_vpn = first
 * 0x00E0CF86-0x00E0CFCC  else splice in between head and its next
 *                        (tail): tail = mmape[head].next_vpn;
 *                        mmape[head].next_vpn = last; mmape[tail].prev_vpn
 *                        = first; mmape[first].next_vpn = tail;
 *                        mmape[last].prev_vpn = (word)head_vpn (re-read
 *                        from (0xE,A0) = MMAP_$WSL[0].head_vpn low word)
 * 0x00E0CFD2-0x00E0CFD6  MMAP_$WSL[0].page_count += count
 * 0x00E0CFDA-0x00E0CFE2  ML_$SPIN_UNLOCK(&MMAP_GLOBALS.lock, token)
 *
 * The 0xE0CFF4 cell (00 06 00 08) is only referenced from here.
 */

#include "mmap/mmap_internal.h"
#include "misc/misc.h"

/*
 * 0x00E0CED2: pea (0x120,PC) -> 0x00E0CFF4, jsr CRASH_SYSTEM at 0x00E0CED6.
 * Image bytes 00 06 00 08 = status_$mmap_inconsistent_mmape.
 */
static const status_$t mmap_$inconsistent_mmape_00e0cff4 =
    status_$mmap_inconsistent_mmape;

void MMAP_$FREE_PAGES(uint16_t pid, uint32_t *vpn_array, uint16_t count)
{
    ml_$spin_token_t token;
    ws_hdr_t *free_pool = &MMAP_$WSL[MMAP_WSL_POOL_FREE];    /* 0x00E0CE6C */
    uint32_t first;     /* (-0x1C,A6) */
    uint32_t last;      /* (-0x18,A6) */
    uint32_t next;      /* (-0x14,A6) */
    uint32_t prev;      /* (-0x10,A6) */
    uint32_t n;         /* (-0x38,A6) */
    uint16_t i;         /* D3 */

    (void)pid; /* (0x8,A6) is never read */

    token = ML_$SPIN_LOCK(&MMAP_GLOBALS.lock);                 /* 0x00E0CE74 */

    first = vpn_array[0];                                       /* 0x00E0CE82 */
    n = count;                                                  /* 0x00E0CE86 */
    last = vpn_array[count - 1];                                /* 0x00E0CE92 */
    next = first;                                               /* 0x00E0CE98 */
    prev = 0;                                                   /* 0x00E0CE9E */

    if (count != 0) {                                           /* 0x00E0CEA2 */
        /* 0x00E0CEB8-0x00E0CF4A, count iterations */
        for (i = 1; i <= count; i++) {
            uint32_t vpn;
            mmape_t *page;
            ws_hdr_t *wsl;
            uint16_t page_next;
            uint16_t page_prev;

            vpn = next;                                         /* 0x00E0CEB8 */
            if (i < count) {                                    /* 0x00E0CEBC */
                next = vpn_array[i];                            /* 0x00E0CEC0 */
            }

            page = MMAPE_FOR_VPN(vpn);                          /* 0x00E0CEC4 */
            if (!(page->flags1 & MMAPE_FLAG1_IN_WSL)) {         /* 0x00E0CECC */
                CRASH_SYSTEM(&mmap_$inconsistent_mmape_00e0cff4); /* 0x00E0CED6 */
            }

            wsl = &MMAP_$WSL[page->wsl_index];                  /* 0x00E0CEDE */

            page_next = page->next_vpn;                         /* 0x00E0CEF8 */
            page_prev = page->prev_vpn;                         /* 0x00E0CEFC */
            MMAPE_FOR_VPN(page_prev)->next_vpn = page_next;     /* 0x00E0CF08 */
            MMAPE_FOR_VPN(page_next)->prev_vpn = page_prev;     /* 0x00E0CF14 */

            if (vpn == wsl->head_vpn) {                         /* 0x00E0CF18 */
                wsl->head_vpn = page->prev_vpn;                 /* 0x00E0CF24 */
            }

            wsl->page_count--;                                  /* 0x00E0CF28 */

            page->prev_vpn = (uint16_t)next;                    /* 0x00E0CF2C */
            page->next_vpn = (uint16_t)prev;                    /* 0x00E0CF32 */
            page->wsl_index = MMAP_WSL_POOL_FREE;               /* 0x00E0CF38 */
            page->flags2 &= 0x3F;                               /* 0x00E0CF3C */

            prev = vpn;                                         /* 0x00E0CF42 */
        }
    }

    if (free_pool->page_count == 0) {                           /* 0x00E0CF52 */
        MMAPE_FOR_VPN(first)->next_vpn = (uint16_t)last;        /* 0x00E0CF68 */
        MMAPE_FOR_VPN(last)->prev_vpn = (uint16_t)first;        /* 0x00E0CF78 */
        free_pool->head_vpn = first;                            /* 0x00E0CF7E */
    } else {
        uint32_t head = free_pool->head_vpn;                    /* 0x00E0CF86 */
        mmape_t *head_page = MMAPE_FOR_VPN(head);
        uint16_t tail = head_page->next_vpn;                    /* 0x00E0CF98 */

        head_page->next_vpn = (uint16_t)last;                   /* 0x00E0CF9C */
        MMAPE_FOR_VPN(tail)->prev_vpn = (uint16_t)first;        /* 0x00E0CFAA */
        MMAPE_FOR_VPN(first)->next_vpn = tail;                  /* 0x00E0CFBA */
        MMAPE_FOR_VPN(last)->prev_vpn = (uint16_t)free_pool->head_vpn; /* 0x00E0CFCC */
    }

    free_pool->page_count += n;                                 /* 0x00E0CFD6 */

    ML_$SPIN_UNLOCK(&MMAP_GLOBALS.lock, token);                 /* 0x00E0CFDA */
}
