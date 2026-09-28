/*
 * MMAP_$WS_SCAN - Scan a working set and steal pages from it
 *
 * Original address: 0x00E0D364 (646 bytes; `E0D364 MMAP_$WS_SCAN` in the
 * SAU2 map).  Callers: 0x00E144CA, 0x00E1457E, 0x00E145CC (PMAP), each
 * pushing two longwords, a boolean word and the index word.
 *
 * Frame (0x00E0D364-0x00E0D36C): `link.w A6,#-0x38`, D2-D7/A2-A5 saved,
 * A5 = the MMAP_ block (0xE23284).  Arguments, (0x8,A6) being argument 1:
 *   (0x08,A6)  wsl_index     word
 *   (0x0A,A6)  mode          boolean, byte in the high half of its word
 *                            slot: true = take dirty pages of flushable
 *                            objects, false = take unreferenced pages
 *   (0x0C,A6)  pages_needed  longword
 *   (0x10,A6)  unused        longword pushed by every caller, never read
 *
 * Pascal function: the result is D0 = pages examined (D5).
 *
 * Registers/locals: D2 = the page under examination (starts at head_vpn),
 * D3 = its prev_vpn (the next one), D4 = pages removed, D5 = pages
 * examined, (-0x8,A6) = the page count at entry; four chains collected
 * through next_vpn, terminated by 0: D6 -> pool 2 (impure), (-0x20,A6) ->
 * pool 1 (pure), (-0x1C,A6) -> pool 3 (dirty local), (-0x18,A6) -> pool 4
 * (dirty remote); the `move.w (-0x1e/-0x1a/-0x16,A6)' stores are the low
 * words of those chain heads.  A3 = the mmape_t (0xEB4800 + vpn*0x10 with
 * -0x2000-based displacements), A0 = PMAP_SEGMAP[segment][seg_offset]
 * reached as 0xED5000 + segment*0x80 + seg_offset*4 with a -0x80
 * displacement, and 0xFFB800 + vpn*4 + 2 is the PFT second word.
 *
 * 0x00E0D372-0x00E0D38C  wsl_index < 5 or > MMAP_$WSL_HI_MARK ->
 *                        CRASH_SYSTEM(&mmap_$illegal_wsl_index_00e0c9e0)
 * 0x00E0D38E             MMAP_$WS_SCAN_CNT++ (0x18,A5)
 * 0x00E0D3BE/0x00E0D568  loop while examined < page_count (unsigned `bcs');
 *                        after each page also stop once removed >=
 *                        pages_needed (0x00E0D562, unsigned `bcc') - that
 *                        test is skipped on entry
 * 0x00E0D3F4-0x00E0D44A  mode true: skip unless (flags2 bit 6 or PFT bit
 *                        14) - dirty - and flags2 bit 7 (ON_DISK) clear and
 *                        bit 12 of the owning object's attribute word
 *                        (MMAP_$SEG_ASTE_FOR(segment)->aote + 0x0E) set
 * 0x00E0D44C-0x00E0D466  mode false: PFT bit 13 (REFERENCED) set -> clear
 *                        it (`andi.w #0xdfff') and skip; else take
 * 0x00E0D46A-0x00E0D490  take: removed++; unlink - mmape[next_vpn]
 *                        .prev_vpn = D3; mmape[D3].next_vpn = next_vpn
 * 0x00E0D496-0x00E0D49A  wired page (wire_count != 0): 0x00E0D554 -
 *                        MMAP_$PAGEABLE_PAGES--, flags1 &= ~0x80, and it
 *                        is simply dropped (no chain)
 * 0x00E0D49E-0x00E0D4B6  seg-map word bit 13 (flags byte bit 5,
 *                        PMAP_SEGMAP_INSTALLED) set -> clear it (`bclr.b
 *                        #5') and MMU_$REMOVE(vpn)
 * 0x00E0D4B8-0x00E0D4D2  dirty = PFT bit 14 or flags2 bit 6
 * 0x00E0D4D4-0x00E0D536  dirty: flags2 bit 7 (ON_DISK) set -> `tst.w
 *                        (0x28,A1) / sne' = high word of aote->dtm_high
 *                        non-zero; clear -> `tst.w (0x8,A1) / smi' = high
 *                        word of aote->location negative (bit 31).  true ->
 *                        chain 4, false -> chain 3.  Note the second test
 *                        differs from mmap_$trim_wsl's `tst.b (0xb9)'.
 * 0x00E0D538-0x00E0D552  clean: flags1 bit 6 (IMPURE) -> chain 1, else
 *                        chain 2 (D6)
 * 0x00E0D55E-0x00E0D560  D2 = D3; examined++
 * 0x00E0D570-0x00E0D588  page_count -= removed; scan_pos (+0x08): examined
 *                        < scan_pos -> scan_pos -= examined, else 0;
 *                        head_vpn = D2; MMAP_$WS_REMOVE (0xC,A5) += removed
 * 0x00E0D58C-0x00E0D5DA  the non-empty chains handed to the nested
 *                        procedure mmap_$move_pages_to_wsl_type in the
 *                        order 2, 1, 3, 4, each with a 2-byte Pascal result
 *                        slot that is never filled
 * 0x00E0D5DE             result = examined
 *
 * Removed unwired pages keep flags1 bit 7 and MMAP_$PAGEABLE_PAGES is not
 * touched for them: they stay "pageable", now on a global pool.
 */

#include "mmap/mmap_internal.h"
#include "misc/misc.h"
#include "mmu/mmu.h"

uint32_t MMAP_$WS_SCAN(uint16_t wsl_index, int16_t mode, uint32_t pages_needed,
                       uint32_t unused)
{
    ws_hdr_t *wsl;
    uint32_t vpn;            /* D2 */
    uint32_t next;           /* D3 */
    uint32_t removed;        /* D4 */
    uint32_t examined;       /* D5 */
    uint32_t chain_impure;   /* D6 */
    uint32_t chain_pure;     /* (-0x20,A6) */
    uint32_t chain_local;    /* (-0x1C,A6) */
    uint32_t chain_remote;   /* (-0x18,A6) */
    uint32_t page_count;     /* (-0x8,A6) */

    (void)unused; /* (0x10,A6) is never read */

    if (wsl_index < WSL_INDEX_MIN_USER || wsl_index > MMAP_$WSL_HI_MARK) { /* 0x00E0D376 */
        CRASH_SYSTEM(&mmap_$illegal_wsl_index_00e0c9e0);     /* 0x00E0D386 */
    }

    MMAP_$WS_SCAN_CNT++;                                     /* 0x00E0D38E */
    chain_impure = 0;                                        /* 0x00E0D392 */
    chain_local = 0;
    chain_remote = 0;
    chain_pure = 0;
    removed = 0;                                             /* 0x00E0D3A0 */
    examined = 0;                                            /* 0x00E0D3A2 */

    wsl = &MMAP_$WSL[wsl_index];                             /* 0x00E0D3A4 */
    page_count = wsl->page_count;                            /* 0x00E0D3B4 */
    vpn = wsl->head_vpn;                                     /* 0x00E0D3BA */

    while (examined < page_count) {                          /* 0x00E0D568 */
        mmape_t *page = MMAPE_FOR_VPN(vpn);                  /* 0x00E0D3C2 */
        pmap_segmap_entry_t *seg_entry =
            &PMAP_SEGMAP[page->segment][page->seg_offset];   /* 0x00E0D3D2 */
        uint16_t *pft = PMAPE_FOR_VPN(vpn);
        boolean take = false;

        next = page->prev_vpn;                               /* 0x00E0D3F0 */

        if (mode < 0) {                                      /* 0x00E0D3F4 */
            boolean dirty = (page->flags2 & MMAPE_FLAG2_MODIFIED) ? true : false; /* 0x00E0D400 */

            if (pft[1] & PMAPE_FLAG_MODIFIED) {              /* 0x00E0D410 */
                dirty = true;
            }
            if (dirty < 0                                    /* 0x00E0D418 */
                && !(page->flags2 & MMAPE_FLAG2_ON_DISK)) {  /* 0x00E0D41C */
                aote_t *aote = MMAP_$SEG_ASTE_FOR(page->segment)->aote; /* 0x00E0D43A */

                if (MMAP_AOTE_ATTR_FLAGS(aote) & MMAP_AOTE_ATTR_FLAG_BIT12) { /* 0x00E0D442 */
                    take = true;
                }
            }
        } else {
            if (pft[1] & PMAPE_FLAG_REFERENCED) {            /* 0x00E0D45A */
                pft[1] &= (uint16_t)~PMAPE_FLAG_REFERENCED;  /* 0x00E0D460 */
            } else {
                take = true;
            }
        }

        if (take < 0) {
            uint16_t next_vpn;

            removed++;                                       /* 0x00E0D46C */
            next_vpn = page->next_vpn;                       /* 0x00E0D46E */
            MMAPE_FOR_VPN(next_vpn)->prev_vpn = (uint16_t)next; /* 0x00E0D47E */
            MMAPE_FOR_VPN(next)->next_vpn = next_vpn;        /* 0x00E0D490 */

            if (page->wire_count != 0) {                     /* 0x00E0D496 */
                MMAP_$PAGEABLE_PAGES--;                      /* 0x00E0D554 */
                page->flags1 &= (uint8_t)~MMAPE_FLAG1_IN_WSL; /* 0x00E0D558 */
            } else {
                boolean dirty;

                if (seg_entry->flags & PMAP_SEGMAP_INSTALLED) { /* 0x00E0D4A2 */
                    seg_entry->flags &= (uint8_t)~PMAP_SEGMAP_INSTALLED; /* 0x00E0D4A8 */
                    MMU_$REMOVE(vpn);                        /* 0x00E0D4B0 */
                }

                dirty = (pft[1] & PMAPE_FLAG_MODIFIED) ? true : false; /* 0x00E0D4C6 */
                if (page->flags2 & MMAPE_FLAG2_MODIFIED) {   /* 0x00E0D4CC */
                    dirty = true;
                }

                if (dirty < 0) {
                    aote_t *aote = MMAP_$SEG_ASTE_FOR(page->segment)->aote;
                    boolean needs_flush;

                    if (page->flags2 & MMAPE_FLAG2_ON_DISK) { /* 0x00E0D4D4 */
                        needs_flush = ((aote->dtm_high >> 16) != 0) ? true : false; /* 0x00E0D4F4 */
                    } else {
                        needs_flush = (aote->location & 0x80000000u) ? true : false; /* 0x00E0D516 */
                    }

                    if (needs_flush < 0) {                   /* 0x00E0D51C */
                        page->next_vpn = (uint16_t)chain_remote; /* 0x00E0D520 */
                        chain_remote = vpn;                  /* 0x00E0D526 */
                    } else {
                        page->next_vpn = (uint16_t)chain_local; /* 0x00E0D52C */
                        chain_local = vpn;                   /* 0x00E0D532 */
                    }
                } else if (page->flags1 & MMAPE_FLAG1_IMPURE) { /* 0x00E0D538 */
                    page->next_vpn = (uint16_t)chain_pure;   /* 0x00E0D540 */
                    chain_pure = vpn;                        /* 0x00E0D546 */
                } else {
                    page->next_vpn = (uint16_t)chain_impure; /* 0x00E0D54C */
                    chain_impure = vpn;                      /* 0x00E0D550 */
                }
            }
        }

        vpn = next;                                          /* 0x00E0D55E */
        examined++;                                          /* 0x00E0D560 */
        if (removed >= pages_needed) {                       /* 0x00E0D562 */
            break;
        }
    }

    wsl->page_count -= removed;                              /* 0x00E0D570 */
    if (examined < wsl->scan_pos) {                          /* 0x00E0D574 */
        wsl->scan_pos -= examined;                           /* 0x00E0D580 */
    } else {
        wsl->scan_pos = 0;                                   /* 0x00E0D57A */
    }
    wsl->head_vpn = vpn;                                     /* 0x00E0D584 */
    MMAP_$WS_REMOVE += removed;                              /* 0x00E0D588 */

    if (chain_impure != 0) {                                 /* 0x00E0D58C */
        mmap_$move_pages_to_wsl_type(chain_impure, MMAP_WSL_POOL_IMPURE,
                                     wsl_index, mode);       /* 0x00E0D598 */
    }
    if (chain_pure != 0) {                                   /* 0x00E0D59E */
        mmap_$move_pages_to_wsl_type(chain_pure, MMAP_WSL_POOL_PURE,
                                     wsl_index, mode);       /* 0x00E0D5AE */
    }
    if (chain_local != 0) {                                  /* 0x00E0D5B4 */
        mmap_$move_pages_to_wsl_type(chain_local, MMAP_WSL_POOL_DIRTY_LOCAL,
                                     wsl_index, mode);       /* 0x00E0D5C4 */
    }
    if (chain_remote != 0) {                                 /* 0x00E0D5CA */
        mmap_$move_pages_to_wsl_type(chain_remote, MMAP_WSL_POOL_DIRTY_RMT,
                                     wsl_index, mode);       /* 0x00E0D5DA */
    }

    return examined;                                         /* 0x00E0D5DE */
}
