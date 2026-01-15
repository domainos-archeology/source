/*
 * MMAP_$INIT - Initialize MMAP subsystem
 *
 * Initializes the memory map management subsystem. Sets up the
 * working set list headers, scans physical memory to build the
 * free page pool, and initializes page table entries.
 *
 * Original address: 0x00e3193e
 */

#include "mmap_internal.h"
#include "mmu/mmu.h"
#include "misc/misc.h"

void MMAP_$INIT(void *param)
{
    uint32_t *mmape_phys_table = (uint32_t*)param;  /* Table of physical addresses */

    /* Clear the owner tracking array (65 entries) */
    for (int i = 0; i < 65; i++) {
        MMAP_PID_TO_WSL[i] = 0;
    }

    /* Set initial WS owner */
    MMAP_$WS_OWNER = 7;

    /* Initialize 70 working set list headers */
    for (int i = 0; i < 70; i++) {
        ws_hdr_t *wsl = WSL_FOR_INDEX(i);
        wsl->flags &= 0x07;       /* Clear all but low 3 bits */
        wsl->page_count = 0;
        wsl->scan_pos = 0;
        wsl->max_pages = 0x1000;  /* Default max */
        wsl->field_14 = 0;
        wsl->pri_timestamp = 0;
        wsl->owner = 0;
        wsl->ws_timestamp = 0;
    }

    /* Initialize the mmape physical address table (56 entries) */
    for (int i = 0; i < 56; i++) {
        mmape_phys_table[i] = 0xFFF;  /* Invalid marker */
    }

    /* Scan physical memory pages (0x200 to 0xFFF = 3584 pages) */
    int16_t range_count = 0;
    boolean in_range = false;
    int free_count = 0;

    for (uint32_t vpn = 0x200; vpn <= 0xFFF; vpn++) {
        mmape_t *page = MMAPE_FOR_VPN(vpn);
        page->wsl_index = WSL_INDEX_WIRED;  /* Initially mark as wired */

        uint16_t *pmape = PMAPE_FOR_VPN(vpn);

        /* Check if page is available (has PMAPE_FLAG_MODIFIED set) */
        if ((pmape[0] & 0x4000) == 0 && (int16_t)pmape[0] >= 0) {
            /* Page not available */
            in_range = false;
        } else {
            /* Page is real memory */
            MMAP_$REAL_PAGES++;

            /* Track memory ranges */
            if (!in_range) {
                in_range = true;
                range_count++;
                if (range_count > 3) {
                    CRASH_SYSTEM(MMAP_Error_Examined_Max);
                }
                MEM_EXAM_TABLE[range_count - 1].start = vpn << 10;
            }
            MEM_EXAM_TABLE[range_count - 1].end = vpn << 10;

            /* Clear the physical table entry for this 1KB block */
            int block = (vpn - 0x200) >> 6;  /* 64 pages per block */
            if (block >= 0 && block < 56) {
                mmape_phys_table[block] = 0;
            }
        }

        if (pmape[0] & 0x4000) {
            /* Page is pageable - add to free pool */
            MMAP_$PAGEABLE_PAGES_LOWER_LIMIT++;

            if (vpn < MMAP_$LPPN) {
                MMAP_$LPPN = vpn;
            }
            if (vpn > MMAP_$HPPN) {
                MMAP_$HPPN = vpn;
            }

            page->flags1 |= MMAPE_FLAG1_IN_WSL;
            page->wsl_index = WSL_INDEX_FREE_POOL;

            if (free_count == 0) {
                /* First free page - point to self */
                page->prev_vpn = (uint16_t)vpn;
                page->next_vpn = (uint16_t)vpn;
                WSL_FOR_INDEX(WSL_INDEX_FREE_POOL)->head_vpn = vpn;
            } else {
                /* Add to circular list */
                uint32_t head = WSL_FOR_INDEX(WSL_INDEX_FREE_POOL)->head_vpn;
                mmape_t *head_page = MMAPE_FOR_VPN(head);
                uint16_t tail = head_page->next_vpn;

                page->prev_vpn = (uint16_t)head;
                page->next_vpn = tail;
                head_page->next_vpn = (uint16_t)vpn;
                MMAPE_FOR_VPN(tail)->prev_vpn = (uint16_t)vpn;
            }

            free_count++;
            page->wire_count = 0;
        } else {
            page->wire_count = 1;  /* Mark as wired/unavailable */
        }
    }

    WSL_FOR_INDEX(WSL_INDEX_FREE_POOL)->page_count = free_count;

    /* Convert mmape table entries to physical addresses */
    for (int i = 0; i < 56; i++) {
        if (mmape_phys_table[i] != 0) {
            void *vaddr = (char*)MMAPE_BASE + (i * 0x400);
            mmape_phys_table[i] = mmu_$vtop_or_crash((uint32_t)(uintptr_t)vaddr);
        }
    }

    /* Clear high bits of memory range entries */
    for (int i = 0; i < range_count; i++) {
        MEM_EXAM_TABLE[i].start &= 0x0007FFFF;
    }

    /* Mark certain WSL indices as in-use */
    WSL_FOR_INDEX(4)->flags |= WSL_FLAG_IN_USE;  /* 0x80 */
    WSL_FOR_INDEX(5)->flags |= (WSL_FLAG_IN_USE | 0x20);  /* 0xA0 */

    /* Set default max for pure page pool */
    WSL_FOR_INDEX(2)->max_pages = 100;
}
