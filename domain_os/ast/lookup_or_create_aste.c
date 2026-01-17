/*
 * ast_$lookup_or_create_aste - Look up or create an ASTE for a segment
 *
 * Searches for an existing ASTE for the given segment number in the AOTE's
 * ASTE list. If not found, allocates a new ASTE and loads the file map (FM)
 * from disk.
 *
 * Parameters:
 *   aote - The AOTE to look up/create ASTE in
 *   segment - Segment number
 *   status - Output status
 *
 * Returns: Pointer to ASTE (or NULL on error)
 *
 * Original address: 0x00e0255c
 */

#include "ast/ast_internal.h"

/* Volume reference counts at A5+0x412 */
#if defined(M68K)
#define VOL_REF_COUNTS    ((int16_t *)0xE1E092)
#define VOL_DISMOUNT_MASK (*(uint16_t *)0xE1E0A0)
#define VOL_DISMOUNT_EC   ((ec_$eventcount_t *)0xE1E088)
#else
#define VOL_REF_COUNTS    vol_ref_counts
#define VOL_DISMOUNT_MASK vol_dismount_mask
#define VOL_DISMOUNT_EC   (&vol_dismount_ec)
#endif

aste_t *ast_$lookup_or_create_aste(aote_t *aote, uint16_t segment,
                                    status_$t *status)
{
    aste_t *aste;
    aste_t *existing;
    aste_t *prev;
    uint32_t *segmap;
    uint8_t vol_idx;
    int32_t block_delta;
    int16_t i;

    /* Check volume status for local objects */
    if (*((int8_t *)((char *)aote + 0xB9)) >= 0) {
        vol_idx = *((uint8_t *)((char *)aote + 0xB8));
        if (vol_idx < 0x10 && (VOL_DISMOUNT_MASK & (1 << vol_idx)) != 0) {
            *status = ast_$validate_uid((uid_t *)((char *)aote + 0x10), 0x30F00);
            return NULL;
        }
        /* Increment volume reference count */
        VOL_REF_COUNTS[vol_idx]++;
    }

    /* Increment in-transition count */
    *((uint8_t *)((char *)aote + 0xBE)) += 1;

    /* Allocate new ASTE */
    aste = AST_$ALLOCATE_ASTE();

    /* Set ASTE flags: in-transition, clear busy/dirty/touched */
    *((uint8_t *)((char *)aste + 0x12)) |= 0x80;   /* Set in-transition */
    *((uint8_t *)((char *)aste + 0x12)) &= ~0x40;  /* Clear busy */
    *((uint8_t *)((char *)aste + 0x12)) &= ~0x20;  /* Clear dirty */
    *((uint8_t *)((char *)aste + 0x12)) &= ~0x10;  /* Clear touched */

    /* Set remote flag based on AOTE */
    *((uint8_t *)((char *)aste + 0x12)) &= ~0x08;
    if (*((int8_t *)((char *)aote + 0xB9)) < 0) {
        *((uint8_t *)((char *)aste + 0x12)) |= 0x08;
        AST_$ASTE_R_CNT++;
    } else {
        AST_$ASTE_L_CNT++;
    }

    /* Initialize ASTE fields */
    *((uint8_t *)((char *)aste + 0x10)) = 0;
    *((uint8_t *)((char *)aste + 0x11)) = 0;
    *((aote_t **)((char *)aste + 0x04)) = aote;
    *((uint16_t *)((char *)aste + 0x0C)) = segment;

    /* Log if enabled */
    if (NETLOG_$OK_TO_LOG < 0) {
        NETLOG_$LOG_IT(0, (char *)aote + 0x10, segment, 0,
                       *((uint16_t *)((char *)aste + 0x0E)), 0, 0, 0);
    }

    /* Try to insert into ASTE list, sorted by segment number */
retry:
    existing = *((aste_t **)((char *)aote + 0x04));
    if (existing == NULL || *((uint16_t *)((char *)existing + 0x0C)) < segment) {
        /* Insert at head */
        *((aste_t **)((char *)aote + 0x04)) = aste;
        *((aste_t **)aste) = existing;
        goto inserted;
    }

    /* Search for insertion point */
    while (*((uint16_t *)((char *)existing + 0x0C)) != segment) {
        if (*((aste_t **)existing) == NULL ||
            *((uint16_t *)((char *)(*((aste_t **)existing)) + 0x0C)) < segment) {
            /* Insert after existing */
            *((aste_t **)aste) = *((aste_t **)existing);
            *((aste_t **)existing) = aste;
            goto inserted;
        }
        existing = *((aste_t **)existing);
    }

    /* Found existing ASTE for this segment */
    if (*((int16_t *)((char *)existing + 0x12)) >= 0) {
        /* Not in transition - free our ASTE and return existing */
        AST_$FREE_ASTE(aste);
        *status = status_$ok;
        aste = existing;
        goto done;
    }

    /* Existing is in transition - wait and retry */
    AST_$WAIT_FOR_AST_INTRANS();
    goto retry;

inserted:
    /* Increment ASTE count */
    *((int16_t *)((char *)aote + 0xBC)) += 1;

    /* Get segment map address */
    segmap = (uint32_t *)(SEGMAP_BASE + (uint32_t)*((uint16_t *)((char *)aste + 0x0E)) * 0x80);

    if (*((int8_t *)((char *)aote + 0xB9)) < 0) {
        /* Remote object - zero segment map */
        for (i = 0x1F; i >= 0; i--) {
            *segmap++ = 0;
        }
        *status = status_$ok;
    } else {
        /* Local object - load file map from disk */
        ML_$UNLOCK(AST_LOCK_ID);

        /* Look up VTOCE and get file map info */
        VTOCE_$LOOKUP_FM((char *)aote + 0x9C, segment, -1,
                          (uint32_t *)((char *)aste + 0x08), &block_delta, status);

        if (*status == status_$ok) {
            /* Update block count if needed */
            if (block_delta != 0) {
                ML_$LOCK(PMAP_LOCK_ID);
                *((int32_t *)((char *)aote + 0x24)) += block_delta;
                *((uint8_t *)((char *)aote + 0xBF)) |= 0x20;
                ML_$UNLOCK(PMAP_LOCK_ID);
            }

            /* Read file map from disk */
            FM_$READ((fm_$file_ref_t *)((char *)aote + 0x9C),
                     *((uint32_t *)((char *)aste + 0x08)),
                     segment, (fm_$entry_t *)segmap, status);
        }

        ML_$LOCK(AST_LOCK_ID);

        if (*status == status_$ok) {
            /* Convert disk format to memory format */
            uint32_t *p = segmap;
            for (i = 0x1F; i >= 0; i--) {
                uint32_t entry = *p;
                /* If high bit set in disk format, convert to COW flag */
                if ((int32_t)entry < 0) {
                    *p &= 0x7FFFFFFF;  /* Clear high bit */
                    *((uint8_t *)((char *)p + 1)) |= 0x40;  /* Set COW */
                }
                /* Clear various flags */
                *((uint8_t *)p) &= ~0x20;  /* Clear installed */
                *((uint8_t *)p) &= ~0x80;  /* Clear high bit */
                *((uint8_t *)p) &= ~0x40;  /* Clear COW in high byte */
                *p &= 0xE07FFFFF;          /* Clear reserved bits */
                p++;
            }
        }
    }

    if (*status == status_$ok) {
        /* Success - clear in-transition */
        *((uint8_t *)((char *)aste + 0x12)) &= ~0x80;
        EC_$ADVANCE(&AST_$AST_IN_TRANS_EC);
    } else {
        /* Error - handle special error code */
        if (*status == 0x20006) {
            *status = ast_$validate_uid((uid_t *)((char *)aote + 0x10), 0x20006);
        }

        /* Remove from ASTE list */
        if (aste == *((aste_t **)((char *)aote + 0x04))) {
            *((aste_t **)((char *)aote + 0x04)) = *((aste_t **)aste);
        } else {
            for (prev = *((aste_t **)((char *)aote + 0x04));
                 aste != *((aste_t **)prev);
                 prev = *((aste_t **)prev)) {
            }
            *((aste_t **)prev) = *((aste_t **)aste);
        }

        *((int16_t *)((char *)aote + 0xBC)) -= 1;
        AST_$FREE_ASTE(aste);
        aste = NULL;
    }

done:
    /* Decrement volume reference count for local objects */
    if (*((int8_t *)((char *)aote + 0xB9)) >= 0) {
        VOL_REF_COUNTS[vol_idx]--;
        if (VOL_REF_COUNTS[vol_idx] == 0 &&
            vol_idx < 0x10 &&
            (VOL_DISMOUNT_MASK & (1 << vol_idx)) != 0) {
            EC_$ADVANCE(VOL_DISMOUNT_EC);
        }
    }

    /* Decrement in-transition count */
    *((uint8_t *)((char *)aote + 0xBE)) -= 1;

    return aste;
}
