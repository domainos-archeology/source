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

/* Local netlog function */
static void FUN_00e01502(void);

/* Status codes */
#define status_$disk_write_protected 0x00080007

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
            *dst = *(uint32_t *)(PMAPE_BASE + pmape_offset + 0x0C) & SEGMAP_DISK_ADDR_MASK;

            /* Check PMAPE modified flag (bit 6 at offset 0x0C) */
            if (*(uint16_t *)(PMAPE_BASE + pmape_offset + 0x0C) & 0x40) {
                *dst |= 0x80000000;
            }
        }

        src++;
        dst++;
    }

    ML_$UNLOCK(PMAP_LOCK_ID);

    /* Log if enabled */
    if (NETLOG_$OK_TO_LOG < 0) {
        FUN_00e01502();
    }

    /* Write to disk via FM */
    /* VTOCE pointer at aote + 0x9C */
    FM_$WRITE((char *)aote + 0x9C,
              *((uint32_t *)((char *)aste + 0x08)),  /* VTOCE pointer from ASTE */
              aste->timestamp,                       /* Segment number */
              disk_data,
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

/* Stub for netlog function - to be implemented */
static void FUN_00e01502(void)
{
    /* TODO: Implement netlog call */
}
