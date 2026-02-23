/*
 * pmap_$fill_write_qblks - Fill disk queue blocks with page write descriptors
 *
 * Populates a linked list of disk queue blocks (qblks) with the information
 * needed to write dirty pages to disk. For each page in the input array:
 *
 * 1. Looks up the page's MMAPE entry to find its segment and page index
 * 2. Fills in the queue block with:
 *    - UID of the owning object (or ANON_$UID for anonymous pages)
 *    - Block-in-segment offset
 *    - Current timestamp
 *    - Volume type info
 * 3. If the page has no disk address (disk_addr == 0):
 *    - Searches neighboring pages in the same segment for a nearby address
 *    - Falls back to AOTE base address if no neighbor found
 *    - Allocates a disk block via BAT_$ALLOCATE
 *    - Assigns the allocated block to the page and any adjacent unallocated pages
 *    - Marks the AOTE as needing flush
 * 4. Copies the disk address into the queue block
 * 5. Logs the write via NETLOG_$LOG_IT if enabled
 * 6. Removes MMU mapping if DISK_$DO_CHKSUM is set and checksum bit is on
 *
 * Parameters:
 *   pages    - Array of VPN (virtual page numbers) to write
 *   qblk     - Pointer to first disk queue block (linked list via offset 0x00)
 *   count    - Number of pages to process
 *
 * Original address: 0x00e1327e
 * Size: 798 bytes
 */

#include "pmap/pmap_internal.h"
#include "ast/ast.h"
#include "bat/bat.h"
#include "misc/misc.h"
#include "netlog/netlog.h"

/* Anonymous UID - used for pages with no owning object */
extern uid_t ANON_$UID;

/* AOTE table - array of pointers to AOTE structures, indexed by segment * 0x14 */
#if defined(ARCH_M68K)
    #define AOTE_TABLE_PTR_BASE  0xEC53F0
    /* Segment map base for indexed access */
    #define SEGMAP_INDEXED_BASE  0xED4F80
#else
    extern uint8_t *aote_table_ptr_base;
    extern uint8_t *segmap_indexed_base;
    #define AOTE_TABLE_PTR_BASE  ((uintptr_t)aote_table_ptr_base)
    #define SEGMAP_INDEXED_BASE  ((uintptr_t)segmap_indexed_base)
#endif

/* MMAPE base address for raw pointer arithmetic */
#if defined(ARCH_M68K)
    #define MMAPE_RAW_BASE  0xEB2800
#else
    extern uint8_t *mmape_raw_base;
    #define MMAPE_RAW_BASE  ((uintptr_t)mmape_raw_base)
#endif

void pmap_$fill_write_qblks(int32_t *pages, uint32_t *qblk, int16_t count)
{
    int32_t *page_ptr;
    int16_t remaining;
    int16_t page_index;
    int mmape_offset;
    int aote_slot;
    uint8_t page_idx;
    uint16_t seg_idx;
    uint8_t flags2;
    int32_t aote_ptr;
    uint16_t vol_type;
    uint32_t *segmap_entry;
    uint32_t disk_addr;
    uint32_t hint_addr;
    int16_t extra_count;
    int16_t scan_remaining;
    int32_t *scan_ptr;
    status_$t status[2];
    uint32_t *saved_segmap;
    uint32_t alloc_blocks[18];

    /* Stack-based array for tracking extra pages that need disk addresses.
     * These entries are stored relative to the frame pointer in the original
     * m68k code, using negative offsets from the stack pointer. */
    int32_t extra_mmape_ptrs[16];

    uint32_t *cur_qblk = qblk;
    remaining = count - 1;

    if (remaining < 0) {
        return;
    }

    page_index = 1;
    page_ptr = pages;

    do {
        int32_t vpn = *page_ptr;
        page_ptr++;

        mmape_offset = vpn * 0x10;

        /* Get segment index from MMAPE (offset 0x02 = segment field) */
        seg_idx = *(uint16_t *)(MMAPE_RAW_BASE + mmape_offset + 2);
        aote_slot = (int16_t)(seg_idx * 0x14);

        /* Get page index from MMAPE (offset 0x01 = seg_offset field) */
        page_idx = *(uint8_t *)(MMAPE_RAW_BASE + mmape_offset + 1);

        /* Get flags2 from MMAPE (offset 0x09) - check if page is remote (bit 7) */
        flags2 = *(uint8_t *)(MMAPE_RAW_BASE + mmape_offset + 9);

        if (-((int8_t)flags2 < 0) < 0) {
            /* Remote page (flags2 bit 7 set) - use ANON_$UID */
            cur_qblk[8] = *(uint32_t *)&ANON_$UID;  /* ANON_$UID.high */

            /* Get AOTE pointer */
            aote_ptr = *(int32_t *)(AOTE_TABLE_PTR_BASE + aote_slot);

            /* Use AOTE offset 0x2a as block count */
            cur_qblk[9] = (uint32_t)*(uint16_t *)(aote_ptr + 0x2a);

            /* Clear byte at offset 0x31 in qblk */
            *(uint8_t *)((uintptr_t)cur_qblk + 0x31) = 0;

            /* Volume type from AOTE offset 0x24 */
            vol_type = *(uint16_t *)(aote_ptr + 0x24);
        } else {
            /* Local page - get UID from AOTE */
            aote_ptr = *(int32_t *)(AOTE_TABLE_PTR_BASE + aote_slot);

            /* Copy 8-byte UID from AOTE offset 0x10 */
            cur_qblk[8] = *(uint32_t *)(aote_ptr + 0x10);
            cur_qblk[9] = *(uint32_t *)(aote_ptr + 0x14);

            /* Copy byte from AOTE offset 0x0d */
            *(uint8_t *)((uintptr_t)cur_qblk + 0x31) = *(uint8_t *)(aote_ptr + 0x0d);

            /* Volume type from AOTE offset 0xb8 (single byte) */
            vol_type = (uint16_t)*(uint8_t *)(aote_ptr + 0xb8);
        }

        /* Block-in-segment = AOTE_seg_size * 0x20 + page_idx */
        cur_qblk[10] = (uint32_t)*(uint16_t *)(AOTE_TABLE_PTR_BASE + aote_slot + 8) * 0x20
                       + (uint32_t)page_idx;

        /* Timestamp */
        cur_qblk[0xb] = TIME_$CURRENT_CLOCKH;

        /* Clear first byte at offset 0x30 */
        *(uint8_t *)(cur_qblk + 0xc) = 0;

        /* Zero out 10 bytes starting at offset 0x32 */
        {
            int16_t zero_count = 9;
            uint8_t *zero_ptr = (uint8_t *)((uintptr_t)cur_qblk + 0x32);
            do {
                *zero_ptr = 0;
                zero_count--;
                zero_ptr++;
            } while (zero_count != -1);
        }

        /* Store volume type at offset 0x1f */
        *(uint8_t *)((uintptr_t)cur_qblk + 0x1f) = (uint8_t)vol_type;

        /* Compute segmap entry pointer:
         * segmap_base + segment*0x80 + page_idx*4 - 0x80
         * = 0xED4F80 + segment*0x80 + page_idx*4 */
        segmap_entry = (uint32_t *)(SEGMAP_INDEXED_BASE +
                                    (uint32_t)seg_idx * 0x80 +
                                    (int16_t)((uint16_t)page_idx << 2));
        saved_segmap = segmap_entry;

        /* Check if page has a disk address (disk_addr field, offset 0x0C in MMAPE) */
        disk_addr = *(uint32_t *)(MMAPE_RAW_BASE + mmape_offset + 0x0C) & 0x3FFFFF;

        if (disk_addr == 0) {
            /* Page has no disk address - need to allocate one */

            /* Count extra pages in the same segment that also need disk addresses */
            extra_count = 0;
            scan_remaining = count - page_index;

            if (scan_remaining >= 0) {
                int extra_idx = 0;
                scan_ptr = pages + page_index;

                do {
                    int other_mmape_offset = scan_ptr[-1] * 0x10;

                    /* Check if same segment and no disk address */
                    if (*(int16_t *)(MMAPE_RAW_BASE + other_mmape_offset + 2) ==
                        *(int16_t *)(MMAPE_RAW_BASE + mmape_offset + 2) &&
                        (*(uint32_t *)(MMAPE_RAW_BASE + other_mmape_offset + 0x0C) & 0x3FFFFF) == 0) {
                        extra_count++;
                        extra_mmape_ptrs[extra_idx] = MMAPE_RAW_BASE + other_mmape_offset;
                        extra_idx++;
                    }

                    scan_ptr++;
                    scan_remaining--;
                } while (scan_remaining != -1);
            }

            /* Search for a nearby disk address hint */
            hint_addr = 0;

            ML_$LOCK(PMAP_LOCK_ID);

            /* Search backward through segment map entries before this page */
            {
                int16_t search_count = (int16_t)page_idx - 1;
                uint32_t *search_ptr = segmap_entry;

                if (search_count >= 0) {
                    do {
                        uint16_t *half_ptr = (uint16_t *)((uintptr_t)search_ptr - 2);
                        search_ptr--;

                        if ((*search_ptr & 0x40000000) == 0) {
                            /* Direct entry - use disk address */
                            hint_addr = *search_ptr;
                        } else {
                            /* Indirect entry - look up via VPN in MMAPE */
                            hint_addr = *(uint32_t *)(
                                (uint32_t)*half_ptr * 0x10 + MMAPE_RAW_BASE + 0x0C);
                        }
                        hint_addr &= 0x3FFFFF;
                    } while (hint_addr == 0 && (search_count--, search_count != -1));
                }
            }

            if (hint_addr == 0) {
                /* Search forward through segment map entries after this page */
                int16_t search_count = 0x1F - ((int16_t)page_idx + 1);
                uint32_t *search_ptr = saved_segmap;

                if (search_count >= 0) {
                    do {
                        uint32_t *next_ptr = search_ptr + 1;

                        if ((*next_ptr & 0x40000000) == 0) {
                            hint_addr = *next_ptr;
                        } else {
                            hint_addr = *(uint32_t *)(
                                (uint32_t)*(uint16_t *)((uintptr_t)search_ptr + 6) * 0x10
                                + MMAPE_RAW_BASE + 0x0C);
                        }
                        hint_addr &= 0x3FFFFF;

                        if (hint_addr != 0) goto allocate;

                        search_count--;
                        search_ptr = next_ptr;
                    } while (search_count != -1);
                }

                if (hint_addr == 0) {
                    /* Fall back to AOTE base disk address (offset -0x0C from AOTE slot) */
                    hint_addr = *(uint32_t *)(AOTE_TABLE_PTR_BASE + aote_slot + 4) >> 4;
                }
            }

        allocate:
            ML_$UNLOCK(PMAP_LOCK_ID);

            /* Allocate disk block(s): count = (extra_count << 16) | 1
             * The high word is the extra count, low word is 1 (requesting 1 + extra blocks) */
            BAT_$ALLOCATE(vol_type, hint_addr,
                         ((uint32_t)extra_count << 16) | 1,
                         alloc_blocks, status);

            if (status[0] != 0) {
                CRASH_SYSTEM(status);
            }

            /* Assign allocated blocks to extra pages */
            {
                int16_t assign_count = extra_count - 1;
                int assign_alloc_idx = 0;
                int assign_extra_idx = 0;

                if (assign_count >= 0) {
                    do {
                        int32_t extra_mmape = extra_mmape_ptrs[assign_extra_idx];
                        uint32_t *daddr_ptr = (uint32_t *)(extra_mmape + 0x0C);

                        /* Clear existing disk address bits and set new one */
                        *daddr_ptr &= 0xFFC00000;
                        *daddr_ptr |= alloc_blocks[assign_alloc_idx];

                        assign_count--;
                        assign_alloc_idx++;
                        assign_extra_idx++;
                    } while (assign_count != -1);
                }
            }

            /* Mark AOTE as needing flush (set bit 5 at AOTE slot offset -2) */
            *(uint8_t *)(AOTE_TABLE_PTR_BASE + aote_slot + 0x0E) |= 0x20;
        }

        /* Copy disk address from MMAPE to queue block */
        cur_qblk[1] = *(uint32_t *)(MMAPE_RAW_BASE + mmape_offset + 0x0C) & 0x3FFFFF;

        /* Store VPN in queue block */
        cur_qblk[5] = vpn;

        /* Log the write if NETLOG is enabled */
        if (NETLOG_$OK_TO_LOG < 0) {
            NETLOG_$LOG_IT(3,
                          (void *)&cur_qblk[8],                     /* UID pointer */
                          (int16_t)((uint32_t)cur_qblk[10] >> 5),   /* segment */
                          (int16_t)page_idx,                         /* page index */
                          (int16_t)vpn,                              /* VPN */
                          0, 0, 0);
        }

        /* Advance to next queue block */
        cur_qblk = (uint32_t *)*cur_qblk;

        /* If checksumming is enabled and checksum bit is set in segmap, handle it */
        if (DISK_$DO_CHKSUM < 0 && (*saved_segmap & 0x20000000) != 0) {
            *(uint8_t *)saved_segmap &= 0xDF;  /* Clear checksum bit */
            MMU_$REMOVE(vpn);
        }

        page_index++;
        remaining--;
    } while (remaining != (int16_t)-1);
}
