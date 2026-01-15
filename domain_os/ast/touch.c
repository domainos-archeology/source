/*
 * AST_$TOUCH - Touch (fault in) pages in a segment
 *
 * Brings pages into memory for an ASTE. This is the core page fault
 * handler for the AST subsystem. Handles both local and remote objects.
 *
 * Parameters:
 *   aste - ASTE for the segment
 *   mode - Access mode/concurrency token
 *   page - Starting page number within segment (0-31)
 *   count - Number of pages to touch
 *   ppn_array - Array to receive physical page numbers
 *   status - Status return
 *   flags - Operation flags (0x01=grow, 0x02=?, 0x08=?, 0x20=?)
 *
 * Returns: Number of pages successfully touched
 *
 * Original address: 0x00e030c0
 */

#include "ast/ast_internal.h"
#include "misc/misc.h"
#include "mmap/mmap.h"

uint16_t AST_$TOUCH(aste_t *aste, uint32_t mode, uint16_t page, uint16_t count,
                    uint32_t *ppn_array, status_$t *status, uint16_t flags)
{
    aote_t *aote;
    uint32_t *segmap_ptr;
    uint32_t segmap_base_offset;
    uint16_t pages_touched;
    uint16_t pages_requested;
    uint16_t pages_available;
    uint16_t log_type;
    int i;

    *status = status_$ok;

    aote = aste->aote;

    /* Check for remote object access restrictions */
    if (*(int8_t *)((char *)aote + 0xb9) >= 0) {
        /* Local object */

        /* Check read concurrency */
        if (*(int8_t *)((char *)aote + 0x71) < 0) {
            /* Special access checking */
            if (*(int16_t *)((char *)PROC1_$TYPE + (int16_t)(PROC1_$CURRENT * 2)) == 8) {
                *status = status_$os_only_local_access_allowed;
                return 0;
            }
        }

        /* Check for write concurrency violation */
        uint32_t concurrency = *(uint32_t *)((char *)aote + 0x50);
        if (concurrency != 0 && concurrency != mode && concurrency != 1) {
            if ((*(uint16_t *)((char *)aote + 0x0E) & 0x800) == 0) {
                *status = status_$pmap_read_concurrency_violation;
                return 0;
            }
        }
    }

    /* Mark AOTE and ASTE as busy */
    *(uint8_t *)((char *)aote + 0xBF) |= AOTE_FLAG_BUSY;
    *(uint8_t *)((char *)aste + 0x12) |= 0x40;

    /* Calculate available pages */
    pages_available = 0x20 - page;
    if (count < pages_available) {
        pages_available = count;
    }

    /* Calculate segment map pointer */
    segmap_base_offset = (uint32_t)aste->seg_index * 0x80;
    segmap_ptr = (uint32_t *)(SEGMAP_BASE + segmap_base_offset / sizeof(segmap_entry_t) +
                              page);

    /* Wait for any pages in transition */
    while ((int16_t)*segmap_ptr < 0) {
        ast_$wait_for_page_transition();
    }

    /* Check if page is already installed */
    if ((*segmap_ptr & SEGMAP_FLAG_IN_USE) != 0) {
        /* Page is installed - retrieve from working set */
        pages_touched = 0;
        uint32_t *ppn_out = ppn_array;

        do {
            *(uint8_t *)segmap_ptr |= 0x20;  /* Mark referenced */
            *ppn_out = (uint16_t)*segmap_ptr;
            pages_touched++;
            segmap_ptr++;
            ppn_out++;

            if (pages_touched >= pages_available) break;

            uint16_t entry = *(uint16_t *)segmap_ptr;
            if ((entry & 0x2000) != 0) break;  /* Stop at boundary */
            if ((int16_t)entry < 0) break;      /* Page in transition */
            if ((entry & 0x4000) == 0) break;   /* Not installed */
        } while (1);

        /* Reclaim pages if in range */
        if (*ppn_array > 0x1FF && *ppn_array < 0x1000) {
            MMAP_$RECLAIM(ppn_array, pages_touched, -((flags & 0x20) != 0));
        }

        AST_$WS_FLT_CNT += pages_touched;
        *(uint8_t *)((char *)aote + 0xBF) |= 0x10;  /* Mark touched */
    } else {
        /* Page not installed - need to fault it in */
        log_type = 8;

        if ((*segmap_ptr & 0x400000) != 0) {
            /* Copy-on-write page - handle specially */
            uint16_t cow_count = 0;
            uint32_t *map_ptr = segmap_ptr;

            do {
                *(uint8_t *)map_ptr |= 0x80;  /* Mark in transition */
                cow_count++;
                map_ptr++;

                if (cow_count >= pages_available) break;
                if ((int16_t)*map_ptr < 0) break;
                if ((*map_ptr & SEGMAP_FLAG_IN_USE) != 0) break;
                if ((*map_ptr & 0x400000) == 0) break;
            } while (1);

            /* Call count_valid_pages with flattened parameters
             * per_boot_flag comes from AOTE flags at offset 0x0F */
            pages_touched = ast_$count_valid_pages(aste, cow_count,
                                                   *((uint8_t *)((char *)aote + 0x0F)),
                                                   ppn_array, status);
            *(uint8_t *)((char *)aote + 0xBF) |= 0x10;
        } else {
            /* Normal page fault handling */
            /* Check if within file bounds */
            uint32_t file_size = *(uint32_t *)((char *)aote + 0x20);
            uint32_t page_offset = (uint32_t)page + (uint32_t)aste->segment * 0x20;

            if (file_size == 0 || ((file_size - 1) >> 10) < page_offset) {
                /* Beyond EOF */
                if ((flags & 0x01) == 0) {
                    *status = status_$ast_eof;
                    return 0;
                }

                /* Grow file */
                if ((flags & 0x02) == 0) {
                    if (count == 0x20) {
                        if (pages_available > AST_$GROW_AHEAD_CNT) {
                            pages_available = AST_$GROW_AHEAD_CNT;
                        }
                    } else {
                        pages_available = 1;
                    }
                }
            } else {
                /* Calculate available based on file size */
                uint32_t pages_in_file = ((file_size - 1) >> 10) - page_offset + 1;
                if (pages_in_file < pages_available) {
                    pages_available = pages_in_file;
                }
            }

            log_type = 2;

            /* Mark pages in transition and fault them in */
            uint16_t fault_count = 0;
            uint32_t *map_ptr = segmap_ptr;

            do {
                *(uint8_t *)map_ptr |= 0x80;  /* Mark in transition */
                fault_count++;
                map_ptr++;

                if (fault_count >= pages_available) break;
                if ((int16_t)*map_ptr < 0) break;
                if ((*map_ptr & SEGMAP_FLAG_IN_USE) != 0) break;
                if ((*map_ptr & 0x400000) != 0) break;

                /* Check for remote object or existing disk block */
                if (*(int8_t *)((char *)aote + 0xB9) >= 0) {
                    if ((*map_ptr & 0x3FFFFF) != 0) break;
                }
            } while (1);

            /* Perform the actual fault-in */
            if (*(int8_t *)((char *)aote + 0xB9) < 0) {
                /* Remote object */
                ast_$read_area_pages_network(aste, segmap_ptr, ppn_array, page, fault_count,
                            -((flags & 0x01) != 0), status);
            } else {
                /* Local object */
                *(uint8_t *)((char *)aote + 0xBF) |= 0x10;
                ast_$read_area_pages(aste, segmap_ptr, ppn_array, page, fault_count, status);
            }

            pages_touched = (uint16_t)fault_count;

            /* Update statistics */
            if ((flags & 0x08) == 0) {
                /* Update read fault count */
            } else {
                /* Update write fault count */
            }
        }

        /* Clear transition bits for pages not touched */
        if (pages_touched < pages_available) {
            ast_$clear_transition_bits(segmap_ptr + pages_touched, pages_available - pages_touched);
        }

        if (pages_touched == 0) {
            *(uint8_t *)status |= 0x80;
        } else if (*status == status_$ok) {
            /* Install the pages */
            for (i = 0; i < pages_touched; i++) {
                uint32_t ppn = ppn_array[i];
                if (ppn == 0) {
                    CRASH_SYSTEM(&OS_PMAP_mismatch_err);
                }

                int pmape_offset = ppn * 0x10;
                if (*(int8_t *)(PMAPE_BASE + pmape_offset + 5) < 0) {
                    CRASH_SYSTEM(&OS_MMAP_bad_install);
                }

                /* Set up PMAPE entry */
                *(uint8_t *)(PMAPE_BASE + pmape_offset) = 0;
                *(uint8_t *)(PMAPE_BASE + pmape_offset + 9) &= 0xBF;
                *(uint8_t *)(PMAPE_BASE + pmape_offset + 5) &= 0xBF;

                if ((flags & 0x08) != 0) {
                    *(uint8_t *)(PMAPE_BASE + pmape_offset + 5) |= 0x40;
                }

                *(uint8_t *)(PMAPE_BASE + pmape_offset + 9) &= 0x7F;
                *(uint8_t *)(PMAPE_BASE + pmape_offset + 1) = (uint8_t)page + i;
                *(uint16_t *)(PMAPE_BASE + pmape_offset + 2) = aste->seg_index;
                *(uint32_t *)(PMAPE_BASE + pmape_offset + 0x0C) = *segmap_ptr & 0x7FFFFF;

                /* Update segment map entry */
                *(uint16_t *)(segmap_ptr + 2) = (uint16_t)ppn;
                *(uint8_t *)segmap_ptr |= 0x20;
                *(uint8_t *)segmap_ptr |= 0x40;
                *(uint8_t *)segmap_ptr &= 0x7F;

                segmap_ptr++;
            }

            MMAP_$INSTALL_LIST(ppn_array, pages_touched, -((flags & 0x20) != 0));
            aste->page_count += pages_touched;
            EC_$ADVANCE(&AST_$PMAP_IN_TRANS_EC);
        }

        AST_$PAGE_FLT_CNT += pages_touched;

        /* Log the operation if enabled */
        if (NETLOG_$OK_TO_LOG < 0) {
            NETLOG_$LOG_IT(log_type, (char *)aote + 0x10, aste->segment, page,
                          (uint16_t)*ppn_array, pages_touched,
                          (*(int8_t *)((char *)aote + 0xB9) < 0) ? 1 : 0, 0);
        }
    }

    return pages_touched;
}
