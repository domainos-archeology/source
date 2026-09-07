/*
 * MMAP Internal Helper Functions
 *
 * These are internal functions used by the public MMAP API.
 * They handle the low-level manipulation of the MMAPE entries
 * and WSL doubly-linked lists.
 *
 * Original addresses:
 * - mmap_$add_to_wsl: 0x00e0c514
 * - mmap_$add_pages_to_wsl: 0x00e0c5ae
 * - mmap_$remove_from_wsl: 0x00e0c6e2
 * - mmap_$trim_wsl: 0x00e0c760
 * - mmap_$move_pages_to_wsl_type: 0x00e0d274
 */

#include "mmap/mmap_internal.h"
#include "mmu/mmu.h"
#include "time/time.h"

/*
 * mmap_$add_to_wsl - Add a single page to a working set list
 *
 * Inserts a page into the specified WSL's doubly-linked list.
 * If insert_at_head is negative, insert at tail; otherwise insert after head.
 *
 * @param page: Pointer to the mmape entry
 * @param vpn: Virtual page number
 * @param wsl_index: Target working set list index
 * @param insert_at_head: If negative, insert at tail of list
 */
void mmap_$add_to_wsl(mmape_t *page, uint32_t vpn, uint16_t wsl_index, int8_t insert_at_head)
{
    ws_hdr_t *wsl = WSL_FOR_INDEX(wsl_index);

    /* Mark page as installed in this WSL */
    page->flags1 |= MMAPE_FLAG1_IN_WSL;
    page->wsl_index = (uint8_t)wsl_index;
    page->priority = 0;

    if (wsl->page_count == 0) {
        /* First page in the list - point to self */
        page->prev_vpn = (uint16_t)vpn;
        page->next_vpn = (uint16_t)vpn;
    } else {
        /* Insert into existing circular doubly-linked list */
        uint32_t head_vpn = wsl->head_vpn;
        mmape_t *head = MMAPE_FOR_VPN(head_vpn);
        uint16_t tail_vpn = head->next_vpn;

        head->next_vpn = (uint16_t)vpn;
        page->prev_vpn = (uint16_t)head_vpn;
        page->next_vpn = tail_vpn;
        MMAPE_FOR_VPN(tail_vpn)->prev_vpn = (uint16_t)vpn;

        if (insert_at_head < 0) {
            goto skip_head_update;
        }
    }

    wsl->head_vpn = vpn;

skip_head_update:
    wsl->page_count++;
    MMAP_$PAGEABLE_PAGES++;
}

/*
 * mmap_$add_pages_to_wsl - Add multiple pages to a working set list
 *
 * Inserts an array of pages into the specified WSL. The pages are
 * linked together in the order specified in the array.
 *
 * @param vpn_array: Array of virtual page numbers
 * @param count: Number of pages to add
 * @param wsl_index: Target working set list index
 */
void mmap_$add_pages_to_wsl(uint32_t *vpn_array, uint16_t count, uint16_t wsl_index)
{
    if (count == 0) return;

    uint32_t first_vpn = vpn_array[0];
    uint32_t last_vpn = vpn_array[count - 1];

    mmape_t *first = MMAPE_FOR_VPN(first_vpn);
    mmape_t *last = MMAPE_FOR_VPN(last_vpn);

    /* Set up first page */
    first->flags1 |= MMAPE_FLAG1_IN_WSL;
    first->wsl_index = (uint8_t)wsl_index;
    first->priority = 0;

    if (count > 1) {
        /* Link first to second */
        first->prev_vpn = (uint16_t)vpn_array[1];

        /* Set up middle pages */
        for (uint16_t i = 1; i < count - 1; i++) {
            mmape_t *page = MMAPE_FOR_VPN(vpn_array[i]);
            page->flags1 |= MMAPE_FLAG1_IN_WSL;
            page->wsl_index = (uint8_t)wsl_index;
            page->priority = 0;
            page->prev_vpn = (uint16_t)vpn_array[i + 1];
            page->next_vpn = (uint16_t)vpn_array[i - 1];
        }

        /* Set up last page */
        last->flags1 |= MMAPE_FLAG1_IN_WSL;
        last->wsl_index = (uint8_t)wsl_index;
        last->priority = 0;
        last->next_vpn = (uint16_t)vpn_array[count - 2];
    }

    ws_hdr_t *wsl = WSL_FOR_INDEX(wsl_index);

    if (wsl->page_count == 0) {
        /* Empty list - create circular list */
        first->next_vpn = (uint16_t)last_vpn;
        last->prev_vpn = (uint16_t)first_vpn;
        wsl->head_vpn = first_vpn;
    } else {
        /* Insert into existing list */
        uint32_t head_vpn = wsl->head_vpn;
        mmape_t *head = MMAPE_FOR_VPN(head_vpn);
        uint16_t tail_vpn = head->next_vpn;

        head->next_vpn = (uint16_t)first_vpn;
        first->prev_vpn = (uint16_t)head_vpn;
        last->next_vpn = tail_vpn;
        MMAPE_FOR_VPN(tail_vpn)->prev_vpn = (uint16_t)last_vpn;
    }

    wsl->page_count += count;
    MMAP_$PAGEABLE_PAGES += count;
}

/*
 * mmap_$remove_from_wsl - Remove a page from its working set list
 *
 * Unlinks a page from its WSL's doubly-linked list and marks it
 * as no longer installed.
 *
 * @param page: Pointer to the mmape entry
 * @param vpn: Virtual page number
 */
void mmap_$remove_from_wsl(mmape_t *page, uint32_t vpn)
{
    uint16_t wsl_index = page->wsl_index;
    ws_hdr_t *wsl = WSL_FOR_INDEX(wsl_index);

    uint16_t prev = page->prev_vpn;
    uint16_t next = page->next_vpn;

    /* Unlink from doubly-linked list */
    MMAPE_FOR_VPN(prev)->next_vpn = next;
    MMAPE_FOR_VPN(next)->prev_vpn = prev;

    /* Update head if necessary */
    if (vpn == wsl->head_vpn) {
        wsl->head_vpn = prev;
    }

    wsl->page_count--;
    page->flags1 &= ~MMAPE_FLAG1_IN_WSL;
    MMAP_$PAGEABLE_PAGES--;
}

/*
 * mmap_$trim_wsl - Trim pages from a working set list
 *
 * Removes pages from a WSL until the target number is reached or the
 * list is empty. Pages are collected and then redistributed to appropriate
 * free lists based on their type.
 *
 * @param wsl_index: Working set list to trim
 * @param pages_to_trim: Number of pages to remove
 */
void mmap_$trim_wsl(uint16_t wsl_index, uint32_t pages_to_trim)
{
    ws_hdr_t *wsl = WSL_FOR_INDEX(wsl_index);
    int is_purge = (pages_to_trim == 0x3FFFFF);  /* Magic value for purge all */
    uint32_t skip_count = 0;
    uint32_t removed = 0;
    uint32_t scanned = 0;
    uint32_t remaining = pages_to_trim;
    uint32_t current_vpn = wsl->head_vpn;
    uint32_t free_list_head = 0;  /* For free pages */

    if (!is_purge && pages_to_trim + 0x20 < wsl->page_count) {
        skip_count = 0x20;  /* Skip first 32 pages as a heuristic */
    }

    if (wsl->page_count == 0) goto done_scanning;

    for (uint32_t i = 0; i < wsl->page_count && remaining > 0; i++) {
        mmape_t *page = MMAPE_FOR_VPN(current_vpn);
        uint16_t next_vpn = page->prev_vpn;

        /* Check if we should skip this page (referenced recently) */
        if (i < skip_count) {
            uint16_t *pmape = PMAPE_FOR_VPN(current_vpn);
            if (pmape[1] & PMAPE_FLAG_REFERENCED) {
                current_vpn = next_vpn;
                scanned++;
                continue;
            }
        }

        /* Page can be removed */
        MMAP_$PAGEABLE_PAGES--;
        page->flags1 &= ~MMAPE_FLAG1_IN_WSL;

        /* Unlink from list */
        uint16_t prev = page->next_vpn;
        uint16_t next = page->prev_vpn;
        MMAPE_FOR_VPN(prev)->prev_vpn = next;
        MMAPE_FOR_VPN(next)->next_vpn = prev;

        if (page->wire_count == 0) {
            /* Clear PTE valid bit and remove from TLB */
            uint32_t pte_offset = ((uint32_t)page->seg_offset << 2) +
                                  ((uint32_t)page->segment << 7);
            uint16_t *pte = (uint16_t*)((char*)PTE_BASE + pte_offset - 0x80);
            if (*pte & 0x2000) {  /* If valid */
                *pte &= ~0x20;     /* Clear valid bit */
                MMU_$REMOVE(current_vpn);
            }

            /* Add to free list */
            page->prev_vpn = (uint16_t)free_list_head;
            free_list_head = current_vpn;
        }

        remaining--;
        current_vpn = next_vpn;
        scanned++;
    }

done_scanning:
    /* Move collected pages to appropriate free lists */
    while (free_list_head != 0) {
        mmape_t *page = MMAPE_FOR_VPN(free_list_head);
        uint16_t next = page->prev_vpn;
        uint16_t page_type;

        /*
         * Determine page type for free list placement.
         *
         * 0x00E0C89A: btst.b #0x6,(-0x1ff7,A2)  -- mmape->flags2 bit 6
         * 0x00E0C8AC: move.w (0x2,A1,vpn*4),D1w -- the SECOND word of the
         *             hardware PTE, i.e. PMAPE_FOR_VPN(vpn)[1]
         * 0x00E0C8B0: btst.l #0xe,D1            -- PMAPE_FLAG_MODIFIED
         */
        if ((page->flags2 & MMAPE_FLAG2_MODIFIED) == 0 &&
            (PMAPE_FOR_VPN(free_list_head)[1] & PMAPE_FLAG_MODIFIED) == 0) {
            /* 0x00E0C912: btst.b #0x6,(-0x1ffb,A2) -- mmape->flags1 bit 6 */
            page_type = (page->flags1 & MMAPE_FLAG1_IMPURE) ?
                        MMAP_PAGE_TYPE_PURE : MMAP_PAGE_TYPE_IMPURE;
        } else {
            /*
             * Dirty page: 0x00E0C8B6-0x00E0C910 asks the owning object
             * whether the write has to go over the network.
             *
             *   tst.b (-0x1ff7,A2) / bpl  -- mmape->flags2 bit 7 (ON_DISK)
             *   set:   tst.w (0x28,A3) / sne  -- the HIGH word of
             *                                     aote->len_high
             *   clear: tst.b (0xb9,A3) / smi  -- aote->remote_flag < 0
             *   TRUE -> type 4 (needs flush), FALSE -> type 3.
             */
            aote_t *aote = MMAP_$SEG_ASTE_FOR(page->segment)->aote;
            boolean needs_flush;

            if ((page->flags2 & MMAPE_FLAG2_ON_DISK) != 0) {
                needs_flush = ((aote->len_high >> 16) != 0) ?
                              (boolean)0xFF : 0;
            } else {
                needs_flush = ((int8_t)aote->remote_flag < 0) ?
                              (boolean)0xFF : 0;
            }

            page_type = (needs_flush < 0) ? MMAP_PAGE_TYPE_DIRTY_FL
                                          : MMAP_PAGE_TYPE_DIRTY_NF;
        }

        mmap_$add_to_wsl(page, free_list_head, page_type, 0);
        free_list_head = next;
    }

    /* Update WSL state */
    if (is_purge) {
        wsl->page_count = 0;
        wsl->scan_pos = 0;
        wsl->ws_timestamp = TIME_$CLOCKH;
    } else {
        wsl->page_count = wsl->page_count + remaining - pages_to_trim;
        wsl->head_vpn = current_vpn;
    }
}

/*
 * mmap_$move_pages_to_wsl_type - Move a list of pages to a WSL by type
 *
 * Used after scanning to move pages to their appropriate free lists.
 *
 * This is a nested Pascal procedure of MMAP_$WS_SCAN: it reads the parent
 * frame through the static link (`movea.l (A6),A0` at 0x00E0D284) to get
 * the priority it stamps into each page:
 *
 *   00e0d28c  tst.b (0xa,A0)      ; MMAP_$WS_SCAN's `mode` word, high byte
 *   00e0d290  bpl.b 0x00e0d298    ;   -- a Domain boolean
 *   00e0d292  move.w (0x8,A0),D5w ; MMAP_$WS_SCAN's `wsl_index` word
 *   00e0d298  clr.w D5w           ; otherwise 0
 *   ...
 *   00e0d2b0  move.b D5b,(-0x1ff8,A0)   ; mmape->priority
 *
 * Both are therefore explicit parameters here.
 *
 * @param vpn_head: Head of the page list (linked via next_vpn)
 * @param page_type: Target WSL type (free/pure/impure/dirty)
 * @param scan_wsl_index: MMAP_$WS_SCAN's first argument
 * @param scan_mode: MMAP_$WS_SCAN's second argument (a boolean in its
 *                   high byte, so TRUE reads as a negative int16_t)
 */
void mmap_$move_pages_to_wsl_type(uint32_t vpn_head, uint16_t page_type,
                                  uint16_t scan_wsl_index, int16_t scan_mode)
{
    /* 0x00E0D28C-0x00E0D29A */
    uint8_t priority = (scan_mode < 0) ? (uint8_t)scan_wsl_index : 0;

    ws_hdr_t *wsl = WSL_FOR_INDEX(page_type);
    uint32_t current = vpn_head;
    uint32_t last = 0;
    int count = 0;

    /* Walk the list and set up page entries */
    while (current != 0) {
        count++;
        mmape_t *page = MMAPE_FOR_VPN(current);
        page->prev_vpn = (uint16_t)last;
        page->priority = priority;   /* 0x00E0D2B0 */
        page->wsl_index = (uint8_t)page_type;

        last = current;
        current = page->next_vpn;
    }

    /* Link into WSL */
    if (wsl->page_count == 0) {
        wsl->head_vpn = last;
        MMAPE_FOR_VPN(last)->next_vpn = (uint16_t)vpn_head;
        MMAPE_FOR_VPN(vpn_head)->prev_vpn = (uint16_t)last;
    } else {
        uint32_t head_vpn = wsl->head_vpn;
        mmape_t *head = MMAPE_FOR_VPN(head_vpn);
        uint16_t tail = head->next_vpn;

        head->next_vpn = (uint16_t)vpn_head;
        MMAPE_FOR_VPN(vpn_head)->prev_vpn = (uint16_t)head_vpn;
        MMAPE_FOR_VPN(tail)->prev_vpn = (uint16_t)last;
        MMAPE_FOR_VPN(last)->next_vpn = tail;
    }

    wsl->page_count += count;
}
