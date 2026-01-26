/*
 * AST_$DEACTIVATE_SEGMENT - Deactivate and cleanup a segment (ASTE)
 *
 * Deactivates an ASTE by flushing its pages and removing it from
 * the segment map. Uses ML_$LOCK/ML_$UNLOCK for synchronization
 * and PMAP_$FLUSH to manage physical page mapping.
 *
 * Parameters:
 *   aste   - ASTE to deactivate
 *   flags  - Deactivation flags (high byte and low byte have different meanings)
 *            High byte (param_2 byte 0): purge mode
 *            Low byte (param_2 byte 2): skip update mode
 *   status - Output: status code
 *
 * Original address: 0x00E01950
 * Original size: 386 bytes
 */

#include "ast/ast_internal.h"

/* External references - declared in subsystem headers:
 * PROC1_$CURRENT - proc1/proc1.h
 * PROC1_$TYPE - proc1/proc1.h
 * AST_$AST_IN_TRANS_EC - ast/ast.h (macro)
 */
extern int8_t NETLOG_$OK_TO_LOG;

/* Status codes */
#define status_$ast_segment_not_deactivatable 0x00030004

/* Internal helper for logging */
static void FUN_00e01872(int16_t seg_addr);

void AST_$DEACTIVATE_SEGMENT(aste_t *aste, uint32_t flags, status_$t *status)
{
    uint8_t flags_byte0 = (flags >> 24) & 0xFF;  /* High byte */
    uint8_t flags_byte2 = (flags >> 8) & 0xFF;   /* Third byte */
    uint16_t aste_flags;
    uint32_t segmap_offset;
    uint16_t flush_mode;
    aote_t *aote;

    aste_flags = aste->flags;

    /*
     * Check if ASTE can be deactivated:
     * - Not already in transition (bit 15)
     * - Reference count is 0 (offset 0x11)
     * - Not a system segment that requires OS process
     */
    if ((int16_t)aste_flags < 0) {
        /* In transition */
        *status = status_$ast_segment_not_deactivatable;
        return;
    }

    if (*(uint8_t *)((char *)aste + 0x11) != 0) {
        /* Reference count non-zero */
        *status = status_$ast_segment_not_deactivatable;
        return;
    }

    /* Check for wired+dirty system segment - requires OS process */
    if (((aste_flags & 0x2000) != 0) && ((aste_flags & 0x0800) != 0)) {
        int16_t proc_type = PROC1_$TYPE[PROC1_$CURRENT];
        if (proc_type != 8 && proc_type != 9) {
            *status = status_$ast_segment_not_deactivatable;
            return;
        }
    }

    /* Mark ASTE as in-transition */
    aste->flags |= 0x8000;

    /* Calculate segment map offset */
    segmap_offset = (uint32_t)aste->seg_index * 0x80;

    /* Log if enabled */
    if (NETLOG_$OK_TO_LOG < 0) {
        FUN_00e01872((int16_t)segmap_offset + 0x4F80);
    }

    /* Release AST lock for I/O */
    ML_$UNLOCK(AST_LOCK_ID);

    /* Determine flush mode */
    flush_mode = 1;
    if ((int8_t)flags_byte0 < 0) {
        flush_mode = 3;
    }

    /* Flush segment pages */
    PMAP_$FLUSH(aste, (uint32_t *)(0xED4F80 + segmap_offset), 0, 0x20, flush_mode, status);

    if (*status != status_$ok) {
        goto error_exit;
    }

    /* If not skipping update */
    if ((int8_t)(flags_byte0 & flags_byte2) >= 0) {
        /* Update ASTE or deactivate area based on type */
        if ((aste_flags & 0x1000) != 0) {
            /* Area segment */
            AREA_$DEACTIVATE_ASTE(aste, status);
        } else {
            /* Normal segment - update segment map */
            ast_$update_aste(aste, (segmap_entry_t *)(0xED4F80 + segmap_offset),
                            0, status);
        }

        if (*status != status_$ok) {
            goto error_exit;
        }
    }

    /* Reacquire AST lock */
    ML_$LOCK(AST_LOCK_ID);

    /* If not an area segment, unlink from AOTE's ASTE list */
    if ((aste_flags & 0x1000) == 0) {
        aote = aste->aote;
        if (aote->aste_list == aste) {
            /* ASTE is at head of list */
            aote->aste_list = aste->next;
        } else {
            /* Find ASTE in list and unlink */
            aste_t *prev = aote->aste_list;
            while (prev->next != aste) {
                prev = prev->next;
            }
            prev->next = aste->next;
        }

        /* Decrement AOTE's ASTE count */
        *(int16_t *)((char *)aote + 0xBC) -= 1;
    }

    return;

error_exit:
    /* Set high bit on status to indicate error */
    *(uint8_t *)status |= 0x80;

    /* Reacquire lock and clear in-transition flag */
    ML_$LOCK(AST_LOCK_ID);
    aste->flags &= 0x7FFF;

    /* Signal completion */
    EC_$ADVANCE(&AST_$AST_IN_TRANS_EC);
}

/* Stub for internal logging function */
static void FUN_00e01872(int16_t seg_addr)
{
    /* TODO: Implement segment deactivation logging */
    (void)seg_addr;
}
