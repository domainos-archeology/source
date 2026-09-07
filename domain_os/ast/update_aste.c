/*
 * ast_$update_aste - Write back modified segment map to disk
 *
 * Writes modified segment map entries back to the file map (FM) on disk.
 * Handles conversion from in-memory segment map format to disk format.
 *
 * Parameters:
 *   aste - The ASTE to update
 *   segmap - Segment map entries (32 entries, 4 bytes each)
 *   flags - Update flags (passed to FM_$WRITE)
 *   status - Output status
 *
 * Original address: 0x00e01566
 */

#include "ast/ast_internal.h"

/*
 * ast_$update_aste_log - the nested NETLOG helper at 0x00E01502 (100 bytes)
 *
 * It takes no stack arguments; it reads this function's frame through the
 * static link (`movea.l (A6),A2` at 0x00E0150A):
 *
 *   00e0150e  moveq #0x1f,D0                      ; 32 iterations
 *   00e01514  lea (0x0,A2,D1),A0
 *   00e01518  cmpi.l #-0x80000000,(-0x88,A0)      ; the disk-format image
 *   00e01520  bcs.b 0x00e01524                    ; UNSIGNED compare, so
 *   00e01522  addq.w #0x1,D2w                     ;   bit 31 set -> count it
 *   00e0152a  subq.l #0x2,SP / clr.l -(SP) / clr.w -(SP)   ; p6..p8 = 0
 *   00e01530  move.w D2w,-(SP)                    ; p5 = that count
 *   00e01538  move.b (0x10,A0),D3b                ; p4 = aste->page_count
 *   00e01542  move.w (0xc,A0),-(SP)               ; p3 = aste->timestamp
 *   00e0154e  pea (0x10,A3)                       ; p2 = &aote->uid
 *   00e01552  move.w #0xc,-(SP)                   ; p1 = kind 12
 *   00e01556  jsr 0x00e71b38.l                    ; NETLOG_$LOG_IT
 *
 * A6-0x88 is `disk_data` below (`link.w A6,-0xa0`, and FM_$WRITE is handed
 * `pea (-0x88,A6)` at 0x00E01686), so the flattening passes it and the ASTE
 * explicitly.
 */
static void ast_$update_aste_log(const aste_t *aste,
                                 const uint32_t *disk_data);

/* Status codes */

void ast_$update_aste(aste_t *aste, segmap_entry_t *segmap, uint16_t flags,
                      status_$t *status)
{
    aote_t *aote;
    uint32_t disk_data[32];  /* 128 bytes for disk format */
    uint32_t *src;
    uint32_t *dst;
    int16_t i;

    /* Check if DIRTY flag (0x2000) is set and not AREA (0x0800) */
    if ((aste->flags & ASTE_FLAG_DIRTY) == 0 ||
        (aste->flags & ASTE_FLAG_REMOTE) != 0) {
        *status = status_$ok;
        return;
    }

    /* Get owning AOTE */
    aote = aste->aote;

    /* Clear ASTE dirty bit (0x20 at byte level) */
    *(uint8_t *)&aste->flags &= 0xDF;

    /* Convert segment map to disk format */
    ML_$LOCK(PMAP_LOCK_ID);

    src = (uint32_t *)segmap;
    dst = disk_data;

    for (i = 0x1F; i >= 0; i--) {
        uint32_t entry = *src;

        /* Check if entry is installed (bit 30) */
        if ((entry & SEGMAP_FLAG_IN_USE) == 0) {
            /* Not installed - extract disk address directly */
            *dst = entry & SEGMAP_DISK_ADDR_MASK;

            /* Check copy-on-write bit (bit 22) */
            if (entry & SEGMAP_FLAG_COW) {
                *dst |= 0x80000000;  /* Set modified bit in disk format */
            }
        } else {
            /* Installed - get disk address from PMAPE */
            uint16_t ppn = (uint16_t)entry;  /* Low word is PPN */
            uint32_t pmape_offset = (uint32_t)ppn * 16;

            /* Get disk address from PMAPE (offset 0x0C) */
            *dst = *(uint32_t *)((uintptr_t)MMAPE_BASE + pmape_offset + 0x0C) & SEGMAP_DISK_ADDR_MASK;

            /* Check PMAPE modified flag (bit 6 at offset 0x0C) */
            if (*(uint16_t *)((uintptr_t)MMAPE_BASE + pmape_offset + 0x0C) & 0x40) {
                *dst |= 0x80000000;
            }
        }

        src++;
        dst++;
    }

    ML_$UNLOCK(PMAP_LOCK_ID);

    /* Log if enabled */
    if (NETLOG_$OK_TO_LOG < 0) {
        ast_$update_aste_log(aste, disk_data);
    }

    /* Write to disk via FM */
    /* VTOCE pointer at aote + 0x9C */
    FM_$WRITE((fm_$file_ref_t *)((char *)aote + 0x9C),
              *((uint32_t *)((char *)aste + 0x08)),  /* VTOCE pointer from ASTE */
              aste->timestamp,                       /* Segment number */
              (fm_$entry_t *)disk_data,
              (uint8_t)flags,
              status);

    if (*status != status_$ok) {
        if (*status == status_$disk_write_protected) {
            *status = status_$ok;
        } else {
            /* Set error flag and restore dirty */
            *(uint8_t *)status |= 0x80;
            *(uint8_t *)&aste->flags |= 0x20;
        }
    }
}

static void ast_$update_aste_log(const aste_t *aste,
                                 const uint32_t *disk_data)
{
    int16_t dirty_count = 0;
    int i;

    /* 0x00E0150E-0x00E01526: 32 longwords, count the ones with bit 31 set */
    for (i = 0; i < 32; i++) {
        if (disk_data[i] >= 0x80000000u) {
            dirty_count++;
        }
    }

    /* 0x00E01556 */
    NETLOG_$LOG_IT(12, &aste->aote->uid.high,
                   aste->timestamp,
                   aste->page_count,
                   (uint16_t)dirty_count,
                   0, 0, 0);
}
