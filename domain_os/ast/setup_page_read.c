/*
 * ast_$setup_page_read - Set up pages for reading from disk
 *
 * Prepares segment map entries for disk I/O. Either reserves disk blocks
 * (for non-area objects) or allocates contiguous blocks (for area objects).
 * Updates segment map entries with disk addresses and COW flags.
 *
 * Parameters:
 *   aste - ASTE pointer
 *   segmap - Segment map entries to update
 *   start_page - Starting page number in segment
 *   count - Number of pages
 *   flags - Read flags (bit 6 = don't update size hint)
 *   status - Output status
 *
 * Original address: 0x00e02898
 */

#include "ast/ast_internal.h"

void ast_$setup_page_read(aste_t *aste, uint32_t *segmap, uint16_t start_page,
                          uint16_t count, uint16_t flags, status_$t *status)
{
    aote_t *aote;
    uint8_t vol_idx;
    uint32_t hint;
    uint32_t disk_addrs[34];  /* Space for allocated addresses */
    int16_t i;
    int32_t end_offset;

    aote = *((aote_t **)((char *)aste + 0x04));

    /* Check if object has per-boot flag (bit 1 of byte at offset 0x0F) */
    if ((*((uint8_t *)((char *)aote + 0x0F)) & 2) != 0) {
        *status = status_$ok;
        return;
    }

    vol_idx = *((uint8_t *)((char *)aote + 0xB8));

    /* Check if this is an area (AOTE flags bit 12) */
    if ((*((uint16_t *)((char *)aote + 0x0E)) & 0x1000) == 0) {
        /* Non-area: reserve disk blocks */
        ML_$UNLOCK(PMAP_LOCK_ID);
        BAT_$RESERVE(vol_idx, (uint32_t)count, status);
        ML_$LOCK(PMAP_LOCK_ID);

        if (*status != status_$ok) {
            *((uint8_t *)status) |= 0x80;  /* Set error flag */
            return;
        }

        /* Set COW flag and clear disk address in each entry */
        for (i = count - 1; i >= 0; i--) {
            *((uint8_t *)((char *)segmap + 1)) |= 0x40;  /* Set COW */
            *segmap &= 0xFFC00000;  /* Clear disk address */
            segmap++;
        }

        /* Log if enabled */
        if (NETLOG_$OK_TO_LOG < 0) {
            NETLOG_$LOG_IT(9, (char *)aote + 0x10,
                           *((uint16_t *)((char *)aste + 0x0C)),
                           start_page, 0, 0, count, 0);
        }
    } else {
        /* Area: allocate contiguous blocks */
        /* Calculate allocation hint from previous entry */
        if (start_page == 0 || (*(segmap - 1) & 0x3FFFFF) == 0) {
            /* Use ASTE's disk base as hint */
            hint = *((uint32_t *)((char *)aste + 0x08)) >> 4;
        } else if ((*(segmap - 1) & 0x40000000) == 0) {
            /* Previous entry has direct address */
            hint = *(segmap - 1) & 0x3FFFFF;
        } else {
            /* Previous entry is installed - get address from PMAPE */
            uint16_t ppn = (uint16_t)(*(segmap - 1));
            hint = *(uint32_t *)(PMAPE_BASE + (uint32_t)ppn * 16 + 0x0C) & 0x3FFFFF;
        }

        ML_$UNLOCK(PMAP_LOCK_ID);
        BAT_$ALLOCATE(vol_idx, hint, (uint32_t)count << 16, disk_addrs, status);
        ML_$LOCK(PMAP_LOCK_ID);

        if (*status != status_$ok) {
            *((uint8_t *)status) |= 0x80;  /* Set error flag */
            return;
        }

        /* Set COW flag and copy disk addresses */
        for (i = count - 1; i >= 0; i--) {
            *((uint8_t *)((char *)segmap + 1)) |= 0x40;  /* Set COW */
            *segmap &= 0xFFC00000;  /* Clear old address */
            *segmap |= disk_addrs[count - 1 - i];  /* Copy new address */
            segmap++;
        }

        /* Log each allocation if enabled */
        if (NETLOG_$OK_TO_LOG < 0 && count > 0) {
            for (i = 0; i < count; i++) {
                NETLOG_$LOG_IT(9, (char *)aote + 0x10,
                               *((uint16_t *)((char *)aste + 0x0C)),
                               start_page + i,
                               (uint16_t)(disk_addrs[i] >> 16),
                               (uint16_t)disk_addrs[i],
                               1, 0);
            }
        }
    }

    /* Mark ASTE as dirty */
    *((uint8_t *)((char *)aste + 0x12)) |= 0x20;

    /* Calculate end offset in bytes */
    end_offset = ((uint32_t)*((uint16_t *)((char *)aste + 0x0C)) * 32 +
                  count + start_page - 1) * 0x400;

    if (end_offset < *((int32_t *)((char *)aote + 0x20))) {
        /* Not extending file - check size hint flag */
        if ((flags & 0x40) == 0) {
            *((uint8_t *)((char *)aote + 0xBF)) |= 0x10;  /* Set size hint dirty */
        }
    } else {
        /* Extending file - update size and timestamps */
        *((int32_t *)((char *)aote + 0x20)) = end_offset + 0x400;
        TIME_$CLOCK((clock_t *)((char *)aote + 0x40));
        *((uint32_t *)((char *)aote + 0x28)) = *((uint32_t *)((char *)aote + 0x40));
        *((uint16_t *)((char *)aote + 0x2C)) = *((uint16_t *)((char *)aote + 0x44));
    }

    /* Update block count and dirty flag */
    *((int32_t *)((char *)aote + 0x24)) += count;
    *((uint8_t *)((char *)aote + 0xBF)) |= 0x20;  /* Set dirty flag */
}
