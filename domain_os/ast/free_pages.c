/*
 * AST_$FREE_PAGES - Drop a run of pages from a segment
 *
 * For each segment map entry in start_page..end_page: waits out any
 * transition, remembers an installed page's number (so the nested
 * ast_$flush_installed_pages can unmap and free up to 32 of them at a
 * time) and its MMAPE disk address, or a non-installed entry's disk
 * address; marks the ASTE dirty when anything was there; zeroes the
 * entry; and, when the entry had a disk address and `vol_index` is
 * non-zero, collects the address for BAT_$FREE (32 at a time, with the
 * PMAP lock dropped around the call).
 *
 * Parameters (frame at 0x00E0400C, `link.w A6,-0x118`):
 *   aste       (0x08,A6)
 *   start_page (0x0C,A6)  word (D4)
 *   end_page   (0x0E,A6)  word (D5)
 *   vol_index  (0x10,A6)  word (D3): BAT_$FREE's volume; 0 = keep the blocks
 * Locals: (-0x80,A6) 32 disk addresses, (-0x100,A6) 32 installed ppns,
 *         (-0x108,A6) status, (-0x116,A6) installed count word.
 *
 * Original address: 0x00E0400C (412 bytes), A5 = 0xE1DC80 (unused here).
 */

#include "ast/ast_internal.h"
#include "misc/misc.h"
#include "bat/bat.h"

void AST_$FREE_PAGES(aste_t *aste, int16_t start_page, int16_t end_page,
                     int16_t vol_index)
{
    uint32_t *entry;                /* A2 */
    uint32_t disk_addr;             /* D5 */
    int16_t bat_count;              /* D2w */
    uint16_t installed_count;       /* (-0x116,A6) */
    int16_t remaining;              /* D4w */
    status_$t status;               /* (-0x108,A6) */
    uint32_t installed_pages[32];   /* (-0x100,A6) */
    uint32_t bat_blocks[32];        /* (-0x80,A6) */
    mmape_t *mmape;

    /* 0x00E04022..0x00E04036 */
    ML_$LOCK(PMAP_LOCK_ID);
    bat_count = 0;
    installed_count = 0;

    /* 0x00E0403A..0x00E04054: 0xED5000 + (seg_index << 7) + (start << 2,
     * a 16-bit index) - 0x80 */
    entry = (uint32_t *)((char *)PMAP_SEGMAP_ROW(aste->seg_index)
                         + (int16_t)(start_page << 2));

    /* 0x00E04058..0x00E0406A: 16-bit page count; zero skips the loop */
    remaining = (int16_t)(end_page - start_page + 1);
    if (remaining != 0) {
        remaining--;                            /* dbf counter */
        do {
            /* 0x00E0406E..0x00E0407E: flush what is pending, then wait */
            while ((int32_t)*entry < 0) {
                if (installed_count != 0) {
                    ast_$flush_installed_pages(aste, installed_pages,
                                               &installed_count);
                }
                ast_$wait_for_page_transition();
            }

            /* 0x00E04080..0x00E04086: btst.l #0xe on the high word */
            if (*entry & SEGMAP_VALID) {
                /* 0x00E04088..0x00E040A0: installed - the disk address
                 * comes from the MMAPE (0x3FFFFF of +0x0C) */
                mmape = &MMAPE_BASE[*entry & 0xFFFF];
                disk_addr = mmape->disk_addr & 0x3FFFFF;
                /* 0x00E040A4..0x00E040B4: installed[count++] = ppn (the
                 * store is at (-0x104 + count*4) after the increment, so
                 * the array is 0-based) */
                installed_pages[installed_count] = *entry & 0xFFFF;
                installed_count++;
                /* 0x00E040B8..0x00E040C4: a full batch is flushed now */
                if (installed_count == 0x20) {
                    ast_$flush_installed_pages(aste, installed_pages,
                                               &installed_count);
                }
            } else {
                /* 0x00E040C6..0x00E040CE: not installed - the entry's own
                 * disk address; zero means nothing to mark */
                disk_addr = *entry & 0x3FFFFF;
                if (disk_addr == 0) {
                    goto clear_entry;
                }
            }

            /* 0x00E040D0..0x00E040D4: bset.b #0x5,(0x12,A0) = bit 13 */
            aste->flags |= ASTE_FLAG_DIRTY;

clear_entry:
            /* 0x00E040DA */
            *entry = 0;

            /* 0x00E040DC..0x00E040F8: collect the block for BAT_$FREE */
            if (disk_addr != 0 && vol_index != 0) {
                bat_blocks[bat_count] = disk_addr;      /* (-0x84 + n*4) */
                bat_count++;
                if (bat_count == 0x20) {
                    /* 0x00E040FA..0x00E0414E */
                    if (installed_count != 0) {
                        ast_$flush_installed_pages(aste, installed_pages,
                                                   &installed_count);
                    }
                    ML_$UNLOCK(PMAP_LOCK_ID);
                    BAT_$FREE(bat_blocks, bat_count, vol_index, 1, &status);
                    if (status != status_$ok) {
                        CRASH_SYSTEM(&status);
                    }
                    ML_$LOCK(PMAP_LOCK_ID);
                    bat_count = 0;
                }
            }

            /* 0x00E04150..0x00E04152 */
            entry++;
        } while (remaining-- != 0);
    }

    /* 0x00E04156..0x00E0416C */
    if (installed_count != 0) {
        ast_$flush_installed_pages(aste, installed_pages, &installed_count);
    }
    ML_$UNLOCK(PMAP_LOCK_ID);

    /* 0x00E0416E..0x00E04198: the last partial batch */
    if (bat_count != 0) {
        BAT_$FREE(bat_blocks, bat_count, vol_index, 1, &status);
        if (status != status_$ok) {
            CRASH_SYSTEM(&status);
        }
    }
}
