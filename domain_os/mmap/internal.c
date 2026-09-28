/*
 * MMAP module-local list routines
 *
 * Four procedures of the MMAP_ code segment (`I E0C514 MMAP_ size = 14D8`)
 * that precede the first map symbol MMAP_$SET_WS_PRI and have no symbol of
 * their own, plus the nested procedure of MMAP_$WS_SCAN:
 *
 *   mmap_$add_to_wsl              0x00E0C514  154 bytes
 *   mmap_$add_pages_to_wsl        0x00E0C5AE  308 bytes
 *   mmap_$remove_from_wsl         0x00E0C6E2  126 bytes
 *   mmap_$trim_wsl                0x00E0C760  554 bytes
 *   mmap_$move_pages_to_wsl_type  0x00E0D274  240 bytes (WS_SCAN's nested
 *                                              procedure, static link)
 *
 * None of them loads A5: every `(d,A5)` below is relative to the A5 =
 * 0xE23284 the MMAP_ entry points set, so (0xA14,A5) is
 * MMAP_$PAGEABLE_PAGES and A5 + index*0x24 + 0x2C is MMAP_$WSL[index].
 * The mmape_t array is addressed as 0xEB4800 + vpn*0x10 with -0x2000-based
 * displacements: -0x2000 wire_count, -0x1FFF seg_offset, -0x1FFE segment,
 * -0x1FFC wsl_index, -0x1FFB flags1, -0x1FFA prev_vpn, -0x1FF8 priority,
 * -0x1FF7 flags2, -0x1FF6 next_vpn.
 *
 * List shape: MMAP_$WSL[i].head_vpn is the head; walking prev_vpn from the
 * head visits the whole ring and returns to it; head.next_vpn is the last
 * page of that walk (the "tail").
 */

#include "mmap/mmap_internal.h"
#include "mmu/mmu.h"
#include "time/time.h"

/*
 * mmap_$add_to_wsl - Link one page into a working-set list
 *
 * Original address: 0x00E0C514.  Frame: `link.w A6,#-0x10`, D2-D4/A2/A3
 * saved.  Arguments, (0x8,A6) being argument 1:
 *   (0x08,A6)  page       mmape_t pointer (A0)
 *   (0x0C,A6)  vpn        longword (D0)
 *   (0x10,A6)  wsl_index  word (D1)
 *   (0x12,A6)  at_tail    boolean, byte in the high half of its word slot
 *                         (D2); every caller pushes it with `st -(SP)` /
 *                         `clr.w -(SP)` / the 0x10000 longword
 *
 * 0x00E0C52C-0x00E0C536  A1 = A5 + index*0x24 (biased: (0x30,A1) =
 *                        page_count, (0x38,A1) = head_vpn)
 * 0x00E0C53A-0x00E0C544  flags1 |= 0x80 (IN_WSL); wsl_index = index;
 *                        priority = 0
 * 0x00E0C548-0x00E0C556  empty list: page links to itself, then falls into
 *                        the head store at 0x00E0C592
 * 0x00E0C558-0x00E0C58A  otherwise splice between head and head.next:
 *                        tail = mmape[head].next_vpn; mmape[head].next_vpn
 *                        = vpn; page->prev_vpn = head; page->next_vpn =
 *                        tail; mmape[tail].prev_vpn = vpn
 * 0x00E0C58E-0x00E0C592  at_tail < 0 leaves head_vpn alone; otherwise the
 *                        new page becomes head_vpn
 * 0x00E0C596-0x00E0C5A0  page_count++; MMAP_$PAGEABLE_PAGES++
 */
void mmap_$add_to_wsl(mmape_t *page, uint32_t vpn, uint16_t wsl_index,
                      boolean at_tail)
{
    ws_hdr_t *wsl = &MMAP_$WSL[wsl_index];                   /* 0x00E0C52C */

    page->flags1 |= MMAPE_FLAG1_IN_WSL;                      /* 0x00E0C53A */
    page->wsl_index = (uint8_t)wsl_index;                    /* 0x00E0C540 */
    page->priority = 0;                                      /* 0x00E0C544 */

    if (wsl->page_count == 0) {                              /* 0x00E0C548 */
        page->prev_vpn = (uint16_t)vpn;                      /* 0x00E0C54E */
        page->next_vpn = (uint16_t)vpn;                      /* 0x00E0C552 */
        wsl->head_vpn = vpn;                                 /* 0x00E0C592 */
    } else {
        uint32_t head = wsl->head_vpn;                       /* 0x00E0C558 */
        mmape_t *head_page = MMAPE_FOR_VPN(head);
        uint16_t tail = head_page->next_vpn;                 /* 0x00E0C56C */

        head_page->next_vpn = (uint16_t)vpn;                 /* 0x00E0C570 */
        page->prev_vpn = (uint16_t)head;                     /* 0x00E0C574 */
        page->next_vpn = tail;                               /* 0x00E0C578 */
        MMAPE_FOR_VPN(tail)->prev_vpn = (uint16_t)vpn;       /* 0x00E0C58A */

        if (at_tail >= 0) {                                  /* 0x00E0C58E */
            wsl->head_vpn = vpn;                             /* 0x00E0C592 */
        }
    }

    wsl->page_count = wsl->page_count + 1;                   /* 0x00E0C596 */
    MMAP_$PAGEABLE_PAGES++;                                  /* 0x00E0C5A0 */
}

/*
 * mmap_$add_pages_to_wsl - Link an array of pages into a working-set list
 * as one run
 *
 * Original address: 0x00E0C5AE.  Frame: `link.w A6,#-0x2C`, D2-D6/A2-A4
 * saved.  Arguments, (0x8,A6) being argument 1:
 *   (0x8,A6)  vpn_array  longword array pointer (A0)
 *   (0xC,A6)  count      word (D2; D5 = zero-extended copy)
 *   (0xE,A6)  wsl_index  word (D1)
 *
 * 0x00E0C5C4-0x00E0C5CC  first = vpn_array[0]; last = vpn_array[count-1] -
 *                        read even when count == 0 (no guard in the image)
 * 0x00E0C5EE-0x00E0C5F8  first: flags1 |= 0x80, wsl_index, priority = 0
 * 0x00E0C5FC-0x00E0C600  count <= 1 (unsigned `bls') skips the chaining
 * 0x00E0C602             first->prev_vpn = vpn_array[1]
 * 0x00E0C608-0x00E0C64A  count-1 < 2 skips the middle loop; else `dbf' on
 *                        count-3 = count-2 iterations over vpn_array[1..
 *                        count-2]: flags1 |= 0x80, wsl_index, prev_vpn =
 *                        vpn_array[i+1], priority = 0, next_vpn =
 *                        vpn_array[i-1]
 * 0x00E0C64E-0x00E0C664  last: flags1 |= 0x80, wsl_index, priority = 0,
 *                        next_vpn = vpn_array[count-2]
 * 0x00E0C66A-0x00E0C67C  wsl = &MMAP_$WSL[wsl_index]
 * 0x00E0C67E-0x00E0C68E  empty list: first->next_vpn = last;
 *                        last->prev_vpn = first; head_vpn = first
 * 0x00E0C690-0x00E0C6C6  otherwise splice the run between head and its
 *                        next: tail = mmape[head].next_vpn;
 *                        mmape[head].next_vpn = LAST; mmape[tail].prev_vpn =
 *                        FIRST; first->next_vpn = tail; last->prev_vpn =
 *                        head (the run therefore sits at the end of the
 *                        prev_vpn walk, like a single at_tail add)
 * 0x00E0C6CA-0x00E0C6D4  page_count += count; MMAP_$PAGEABLE_PAGES += count
 */
void mmap_$add_pages_to_wsl(uint32_t *vpn_array, uint16_t count,
                            uint16_t wsl_index)
{
    uint32_t n = count;                                      /* D5 */
    uint32_t first = vpn_array[0];                           /* D3 */
    uint32_t last = vpn_array[count - 1];                    /* D4 */
    mmape_t *first_page = MMAPE_FOR_VPN(first);              /* (-0x1C,A6) */
    mmape_t *last_page = MMAPE_FOR_VPN(last);                /* (-0x20,A6) */
    ws_hdr_t *wsl;
    uint16_t i;

    first_page->flags1 |= MMAPE_FLAG1_IN_WSL;                /* 0x00E0C5EE */
    first_page->wsl_index = (uint8_t)wsl_index;              /* 0x00E0C5F4 */
    first_page->priority = 0;                                /* 0x00E0C5F8 */

    if (count > 1) {                                         /* 0x00E0C5FC */
        first_page->prev_vpn = (uint16_t)vpn_array[1];       /* 0x00E0C602 */

        if ((uint16_t)(count - 1) >= 2) {                    /* 0x00E0C60C */
            /* 0x00E0C61C-0x00E0C64A: count-2 iterations */
            for (i = 1; i <= (uint16_t)(count - 2); i++) {
                mmape_t *page = MMAPE_FOR_VPN(vpn_array[i]);

                page->flags1 |= MMAPE_FLAG1_IN_WSL;          /* 0x00E0C62C */
                page->wsl_index = (uint8_t)wsl_index;        /* 0x00E0C632 */
                page->prev_vpn = (uint16_t)vpn_array[i + 1]; /* 0x00E0C636 */
                page->priority = 0;                          /* 0x00E0C63C */
                page->next_vpn = (uint16_t)vpn_array[i - 1]; /* 0x00E0C640 */
            }
        }

        last_page->flags1 |= MMAPE_FLAG1_IN_WSL;             /* 0x00E0C64E */
        last_page->wsl_index = (uint8_t)wsl_index;           /* 0x00E0C654 */
        last_page->priority = 0;                             /* 0x00E0C658 */
        last_page->next_vpn = (uint16_t)vpn_array[count - 2]; /* 0x00E0C664 */
    }

    wsl = &MMAP_$WSL[wsl_index];                             /* 0x00E0C66A */

    if (wsl->page_count == 0) {                              /* 0x00E0C678 */
        first_page->next_vpn = (uint16_t)last;               /* 0x00E0C682 */
        last_page->prev_vpn = (uint16_t)first;               /* 0x00E0C686 */
        wsl->head_vpn = first;                               /* 0x00E0C68A */
    } else {
        uint32_t head = wsl->head_vpn;                       /* 0x00E0C690 */
        mmape_t *head_page = MMAPE_FOR_VPN(head);
        uint16_t tail = head_page->next_vpn;                 /* 0x00E0C6A4 */

        head_page->next_vpn = (uint16_t)last;                /* 0x00E0C6A8 */
        MMAPE_FOR_VPN(tail)->prev_vpn = (uint16_t)first;     /* 0x00E0C6BA */
        first_page->next_vpn = tail;                         /* 0x00E0C6C2 */
        last_page->prev_vpn = (uint16_t)head;                /* 0x00E0C6C6 */
    }

    wsl->page_count = n + wsl->page_count;                   /* 0x00E0C6CA */
    MMAP_$PAGEABLE_PAGES += n;                               /* 0x00E0C6D4 */
}

/*
 * mmap_$remove_from_wsl - Unlink one page from the working-set list named
 * by its own wsl_index
 *
 * Original address: 0x00E0C6E2.  Frame: `link.w A6,#-0x10`, D2-D5/A2
 * saved.  Arguments, (0x8,A6) being argument 1:
 *   (0x8,A6)  page  mmape_t pointer (A0)
 *   (0xC,A6)  vpn   longword (D0)
 *
 * 0x00E0C6EE-0x00E0C700  wsl = &MMAP_$WSL[page->wsl_index] (byte, zero-
 *                        extended by `clr.w D1')
 * 0x00E0C704-0x00E0C734  prev = page->prev_vpn; next = page->next_vpn;
 *                        mmape[prev].next_vpn = next; mmape[next].prev_vpn
 *                        = prev
 * 0x00E0C738-0x00E0C73E  vpn == head_vpn -> head_vpn = prev
 * 0x00E0C742-0x00E0C752  page_count--; flags1 &= ~0x80;
 *                        MMAP_$PAGEABLE_PAGES--
 */
void mmap_$remove_from_wsl(mmape_t *page, uint32_t vpn)
{
    ws_hdr_t *wsl = &MMAP_$WSL[page->wsl_index];             /* 0x00E0C6F4 */
    uint32_t prev = page->prev_vpn;                          /* 0x00E0C708 */
    uint32_t next = page->next_vpn;                          /* 0x00E0C70C */

    MMAPE_FOR_VPN(prev)->next_vpn = (uint16_t)next;          /* 0x00E0C722 */
    MMAPE_FOR_VPN(next)->prev_vpn = (uint16_t)prev;          /* 0x00E0C734 */

    if (vpn == wsl->head_vpn) {                              /* 0x00E0C738 */
        wsl->head_vpn = prev;                                /* 0x00E0C73E */
    }

    wsl->page_count = wsl->page_count - 1;                   /* 0x00E0C742 */
    page->flags1 &= (uint8_t)~MMAPE_FLAG1_IN_WSL;            /* 0x00E0C74C */
    MMAP_$PAGEABLE_PAGES--;                                  /* 0x00E0C752 */
}

/*
 * mmap_$trim_wsl - Take pages off a working set and drop them on the
 * global pools
 *
 * Original address: 0x00E0C760.  Frame: `link.w A6,#-0x40`, D2-D7/A2/A3
 * saved.  Arguments, (0x8,A6) being argument 1:
 *   (0x8,A6)  wsl_index      word (D0)
 *   (0xA,A6)  pages_to_trim  longword (D6, counted down)
 * Every caller allocates a 2-byte Pascal result slot that nothing here
 * fills; the routine is a procedure.
 *
 * Locals: (-0x26,A6) is_purge = `seq' of pages_to_trim == 0x3FFFFF;
 * (-0x18,A6) skip_count; (-0x2C,A6) the WSL header; (-0x14,A6) the page
 * under examination; D3 = head of the collected list (linked through
 * prev_vpn, 0 terminates); D4 = 1-based position; D5 = 32-bit `dbf'
 * counter (the `clr.w D5w / subq.l #1,D5 / bcc' at 0x00E0C880 extends it
 * past 65536, so the scan makes at most page_count passes).
 *
 * 0x00E0C768-0x00E0C7A4  skip_count = 0x20 when not purging and
 *                        pages_to_trim + 0x20 < page_count (unsigned
 *                        `bcc'), else 0
 * 0x00E0C7A8-0x00E0C7B2  current = head_vpn; an empty list skips the scan
 * 0x00E0C7C0-0x00E0C7F2  page = mmape[current]; A1 = 0xED5000 +
 *                        page->segment*0x80 + page->seg_offset*4, so
 *                        (-0x80,A1) is PMAP_SEGMAP[segment][seg_offset]
 *                        (1-based base 0xED4F80); next = page->prev_vpn
 * 0x00E0C7F6-0x00E0C810  while position <= skip_count, a page whose PFT
 *                        second word (0xFFB800 + vpn*4 + 2) has bit 13
 *                        (REFERENCED) set is passed over without counting
 * 0x00E0C812-0x00E0C842  MMAP_$PAGEABLE_PAGES--; flags1 &= ~0x80; unlink
 *                        (mmape[next_vpn].prev_vpn = prev_vpn,
 *                        mmape[prev_vpn].next_vpn = next_vpn)
 * 0x00E0C846-0x00E0C84A  a wired page (wire_count != 0) is only unlinked -
 *                        it goes on no list
 * 0x00E0C84C-0x00E0C86C  otherwise page->prev_vpn = collected; collected =
 *                        current; if the seg-map word has bit 13 set
 *                        (flags byte bit 5): clear it (`bclr.b #5') and
 *                        MMU_$REMOVE(current)
 * 0x00E0C86E             pages_to_trim-- (both wired and unwired)
 * 0x00E0C870-0x00E0C884  current = next; pages_to_trim == 0 ends the scan;
 *                        position++
 * 0x00E0C88C-0x00E0C946  for each collected page: dirty when flags2 bit 6
 *                        (MODIFIED) or the PFT second word bit 14
 *                        (MODIFIED) is set - then pool 4 when the owning
 *                        object needs a network flush (flags2 bit 7 ON_DISK
 *                        set: `tst.w (0x28,A3) / sne', the high word of
 *                        aote->dtm_high; clear: `tst.b (0xb9,A3) / smi',
 *                        aote->remote_flag < 0), else pool 3; clean pages
 *                        go to pool 1 when flags1 bit 6 is set, else pool
 *                        2.  mmap_$add_to_wsl(page, vpn, pool, false), the
 *                        next collected page read before the call
 * 0x00E0C94E-0x00E0C968  purge: page_count = 0, owner (+2, `clr.w
 *                        (0x2e,A0)') = 0, ws_timestamp (+0x1C) =
 *                        TIME_$CLOCKH (0xE2B0D4)
 * 0x00E0C96A-0x00E0C97A  else page_count += remaining - pages_to_trim
 *                        (the argument re-read from (0xA,A6)); head_vpn =
 *                        current
 */
void mmap_$trim_wsl(uint16_t wsl_index, uint32_t pages_to_trim)
{
    ws_hdr_t *wsl = &MMAP_$WSL[wsl_index];                   /* (-0x2C,A6) */
    boolean is_purge;                                        /* (-0x26,A6) */
    uint32_t skip_count;                                     /* (-0x18,A6) */
    uint32_t remaining = pages_to_trim;                      /* D6 */
    uint32_t current;                                        /* (-0x14,A6) */
    uint32_t collected;                                      /* D3 */
    uint32_t position;                                       /* D4 */
    uint32_t passes;                                         /* D5 + 1 */
    uint32_t pass;

    is_purge = (pages_to_trim == MMAP_TRIM_PURGE_ALL) ? true : false; /* 0x00E0C770 */
    skip_count = 0;                                          /* 0x00E0C77C */
    collected = 0;                                           /* 0x00E0C782 */

    if (is_purge >= 0) {                                     /* 0x00E0C794 */
        if (0x20 + pages_to_trim < wsl->page_count) {        /* 0x00E0C798 */
            skip_count = 0x20;                               /* 0x00E0C7A4 */
        }
    }

    current = wsl->head_vpn;                                 /* 0x00E0C7A8 */

    if (wsl->page_count != 0) {                              /* 0x00E0C7AE */
        passes = wsl->page_count;                            /* 0x00E0C7B6 */
        position = 1;                                        /* 0x00E0C7BC */

        for (pass = 0; pass < passes; pass++) {
            mmape_t *page = MMAPE_FOR_VPN(current);          /* 0x00E0C7C0 */
            pmap_segmap_entry_t *seg_entry =
                &PMAP_SEGMAP[page->segment][page->seg_offset]; /* 0x00E0C7D4 */
            uint32_t next = page->prev_vpn;                  /* 0x00E0C7F2 */
            boolean referenced = false;

            if (position <= skip_count) {                    /* 0x00E0C7F6 */
                if (PMAPE_FOR_VPN(current)[1] & PMAPE_FLAG_REFERENCED) { /* 0x00E0C80C */
                    referenced = true;
                }
            }

            if (referenced >= 0) {
                uint16_t next_vpn;

                MMAP_$PAGEABLE_PAGES--;                      /* 0x00E0C812 */
                page->flags1 &= (uint8_t)~MMAPE_FLAG1_IN_WSL; /* 0x00E0C816 */
                next_vpn = page->next_vpn;                   /* 0x00E0C824 */
                MMAPE_FOR_VPN(next_vpn)->prev_vpn = (uint16_t)next; /* 0x00E0C830 */
                MMAPE_FOR_VPN(next)->next_vpn = next_vpn;    /* 0x00E0C842 */

                if (page->wire_count == 0) {                 /* 0x00E0C846 */
                    page->prev_vpn = (uint16_t)collected;    /* 0x00E0C84C */
                    collected = current;                     /* 0x00E0C854 */
                    if (seg_entry->flags & PMAP_SEGMAP_INSTALLED) { /* 0x00E0C858 */
                        seg_entry->flags &= (uint8_t)~PMAP_SEGMAP_INSTALLED; /* 0x00E0C85E */
                        MMU_$REMOVE(current);                /* 0x00E0C866 */
                    }
                }

                remaining--;                                 /* 0x00E0C86E */
            }

            current = next;                                  /* 0x00E0C870 */
            if (remaining == 0) {                            /* 0x00E0C874 */
                break;
            }
            position++;                                      /* 0x00E0C87A */
        }
    }

    /* 0x00E0C948 / 0x00E0C88C-0x00E0C946 */
    while (collected != 0) {
        mmape_t *page = MMAPE_FOR_VPN(collected);
        uint16_t pool;                                       /* (-0x22,A6) */
        uint32_t next_collected;
        boolean dirty = false;

        if (page->flags2 & MMAPE_FLAG2_MODIFIED) {           /* 0x00E0C89A */
            dirty = true;
        } else if (PMAPE_FOR_VPN(collected)[1] & PMAPE_FLAG_MODIFIED) { /* 0x00E0C8B0 */
            dirty = true;
        }

        if (dirty < 0) {
            aote_t *aote = MMAP_$SEG_ASTE_FOR(page->segment)->aote;
            boolean needs_flush;

            if (page->flags2 & MMAPE_FLAG2_ON_DISK) {        /* 0x00E0C8B6 */
                needs_flush = ((aote->dtm_high >> 16) != 0) ? true : false; /* 0x00E0C8D6 */
            } else {
                needs_flush = (aote->remote_flag < 0) ? true : false; /* 0x00E0C8F8 */
            }

            if (needs_flush < 0) {                           /* 0x00E0C8FE */
                pool = MMAP_WSL_POOL_DIRTY_RMT;              /* 0x00E0C902 */
            } else {
                pool = MMAP_WSL_POOL_DIRTY_LOCAL;            /* 0x00E0C90A */
            }
        } else if (page->flags1 & MMAPE_FLAG1_IMPURE) {      /* 0x00E0C912 */
            pool = MMAP_WSL_POOL_PURE;                       /* 0x00E0C91A */
        } else {
            pool = MMAP_WSL_POOL_IMPURE;                     /* 0x00E0C922 */
        }

        next_collected = page->prev_vpn;                     /* 0x00E0C92A */
        mmap_$add_to_wsl(page, collected, pool, false);      /* 0x00E0C93E */
        collected = next_collected;                          /* 0x00E0C946 */
    }

    if (is_purge < 0) {                                      /* 0x00E0C94E */
        wsl->page_count = 0;                                 /* 0x00E0C958 */
        wsl->owner = 0;                                      /* 0x00E0C95C */
        wsl->ws_timestamp = TIME_$CLOCKH;                    /* 0x00E0C960 */
    } else {
        wsl->page_count = remaining + wsl->page_count - pages_to_trim; /* 0x00E0C96E */
        wsl->head_vpn = current;                             /* 0x00E0C97A */
    }
}

/*
 * mmap_$move_pages_to_wsl_type - Nested procedure of MMAP_$WS_SCAN: hand a
 * next_vpn-linked chain of pages to one of the global pools
 *
 * Original address: 0x00E0D274.  Frame: `link.w A6,#-0x20`, D2-D7/A2
 * saved.  Arguments, (0x8,A6) being argument 1:
 *   (0x8,A6)  vpn_head   longword (D2), chain linked through next_vpn
 *   (0xC,A6)  page_type  word (D3), the destination pool
 * The static link (`movea.l (A6),A0` at 0x00E0D284) reaches MMAP_$WS_SCAN's
 * frame: `tst.b (0xa,A0)` is its second argument (a boolean byte in the
 * high half of the word slot) and `move.w (0x8,A0),D5w` its first (the
 * wsl_index word); both are explicit parameters here.
 *
 * 0x00E0D28C-0x00E0D29A  priority (D5) = scan_wsl_index when scan_mode < 0,
 *                        else 0
 * 0x00E0D29C-0x00E0D2C4  walk the chain: count++ (D1); page->prev_vpn =
 *                        previous page (D0, 0 for the first); page->
 *                        priority = D5; page->wsl_index = page_type; D0 =
 *                        this page; next = page->next_vpn.  flags1 is NOT
 *                        touched and MMAP_$PAGEABLE_PAGES is NOT changed.
 * 0x00E0D2C6-0x00E0D2D8  wsl = &MMAP_$WSL[page_type]
 * 0x00E0D2DA-0x00E0D2FE  empty pool: head_vpn = last; mmape[last].next_vpn
 *                        = vpn_head; mmape[vpn_head].prev_vpn = last
 * 0x00E0D304-0x00E0D352  else: tail = mmape[head].next_vpn;
 *                        mmape[head].next_vpn = vpn_head;
 *                        mmape[vpn_head].prev_vpn = head; mmape[tail]
 *                        .prev_vpn = last; mmape[last].next_vpn = tail
 * 0x00E0D356             page_count += count
 */
void mmap_$move_pages_to_wsl_type(uint32_t vpn_head, uint16_t page_type,
                                  uint16_t scan_wsl_index, int16_t scan_mode)
{
    uint8_t priority = (scan_mode < 0) ? (uint8_t)scan_wsl_index : 0; /* 0x00E0D28C */
    ws_hdr_t *wsl;
    uint32_t count = 0;                                      /* D1 */
    uint32_t current = vpn_head;                             /* D4 */
    uint32_t last = 0;                                       /* D0 */

    while (current != 0) {                                   /* 0x00E0D2C2 */
        mmape_t *page = MMAPE_FOR_VPN(current);

        count++;                                             /* 0x00E0D29C */
        page->prev_vpn = (uint16_t)last;                     /* 0x00E0D2AC */
        page->priority = priority;                           /* 0x00E0D2B0 */
        page->wsl_index = (uint8_t)page_type;                /* 0x00E0D2B4 */
        last = current;                                      /* 0x00E0D2BA */
        current = page->next_vpn;                            /* 0x00E0D2BC */
    }

    wsl = &MMAP_$WSL[page_type];                             /* 0x00E0D2C6 */

    if (wsl->page_count == 0) {                              /* 0x00E0D2D4 */
        wsl->head_vpn = last;                                /* 0x00E0D2DA */
        MMAPE_FOR_VPN(last)->next_vpn = (uint16_t)vpn_head;  /* 0x00E0D2EC */
        MMAPE_FOR_VPN(vpn_head)->prev_vpn = (uint16_t)last;  /* 0x00E0D2FE */
    } else {
        uint32_t head = wsl->head_vpn;                       /* 0x00E0D304 */
        mmape_t *head_page = MMAPE_FOR_VPN(head);
        uint16_t tail = head_page->next_vpn;                 /* 0x00E0D318 */

        head_page->next_vpn = (uint16_t)vpn_head;            /* 0x00E0D31C */
        MMAPE_FOR_VPN(vpn_head)->prev_vpn = (uint16_t)head;  /* 0x00E0D32E */
        MMAPE_FOR_VPN(tail)->prev_vpn = (uint16_t)last;      /* 0x00E0D340 */
        MMAPE_FOR_VPN(last)->next_vpn = tail;                /* 0x00E0D352 */
    }

    wsl->page_count += count;                                /* 0x00E0D356 */
}
