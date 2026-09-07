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
 * NETLOG_$OK_TO_LOG - netlog/netlog.h
 */

/* Status codes */
#define status_$ast_segment_not_deactivatable 0x00030004

/*
 * ast_$deactivate_segment_log - the nested NETLOG helper at 0x00E01872
 *                               (222 bytes)
 *
 * One stack argument of its own -- the address of this segment's 32-entry
 * row in the segment map (`pea (-0x80,A3)` at 0x00E019D4, where A3 is
 * 0xED5000 + seg_index*0x80, so the row base is 0xED4F80 + seg*0x80).  It
 * reaches this function's `aste` argument through the static link
 * (`move.l (A6),D3` at 0x00E0187A, then `movea.l (0x8,A0),A2`), which is
 * passed explicitly here.
 *
 *   00e01884  tst.b (0x10,A2) / beq       ; skip the scan if page_count == 0
 *   00e01890  jsr 0x00e20b12.l            ; ML_$LOCK(0x14)
 *   00e01898  moveq #0x1f,D0              ; 32 segment-map entries
 *   00e018a8  move.w (A0),D1w / btst.l #0xe,D1   ; entry installed?
 *   00e018b0  move.w (0x2,A0),D1w / lsl.w #0x2   ; ppn * 4
 *   00e018b6  move.w (0x2,A2,D1w),D4w     ; the PTE's second word
 *   00e018ba  btst.l #0xd,D4 / beq        ; PMAPE_FLAG_REFERENCED
 *   00e018c0  addq.w #0x1,D2w
 *   00e018ce  jsr 0x00e20b62.l            ; ML_$UNLOCK(0x14)
 *   00e018e0  btst.l #0xc,D0              ; aste->flags & 0x1000
 *   00e018e6  set:   {ANON_$UID.high, (uint16_t)aote+0x2A}
 *   00e01902  clear: the eight bytes at aote+0xA4
 *   00e01940  jsr 0x00e71b38.l            ; NETLOG_$LOG_IT, kind 1
 */
static void ast_$deactivate_segment_log(const aste_t *aste,
                                        const uint32_t *segmap_row);

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

    /*
     * Log if enabled.  0x00E019D4 passes the row base, which is
     * SEGMAP_BASE (0xED4F80) + seg_index * 0x80, i.e. a 32-entry row.
     */
    if (NETLOG_$OK_TO_LOG < 0) {
        ast_$deactivate_segment_log(
            aste, (const uint32_t *)(SEGMAP_BASE + segmap_offset));
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

static void ast_$deactivate_segment_log(const aste_t *aste,
                                        const uint32_t *segmap_row)
{
    const aote_t *aote = aste->aote;
    uint32_t log_uid[2];
    int16_t referenced = 0;
    int i;

    /* 0x00E01884: nothing to count if the segment has no pages */
    if (aste->page_count != 0) {
        /* 0x00E01890 */
        ML_$LOCK(PMAP_LOCK_ID);

        for (i = 0; i < 32; i++) {
            uint32_t entry = segmap_row[i];

            /* 0x00E018AA: btst.l #0xe on the entry's HIGH word */
            if ((entry & 0x40000000u) != 0) {
                /* 0x00E018B0: the low word is the PPN */
                uint16_t ppn = (uint16_t)entry;

                /* 0x00E018B6/0x00E018BA: the PTE's second word, bit 13 */
                if ((PMAPE_FOR_VPN(ppn)[1] & PMAPE_FLAG_REFERENCED) != 0) {
                    referenced++;
                }
            }
        }

        /* 0x00E018CE */
        ML_$UNLOCK(PMAP_LOCK_ID);
    }

    /* 0x00E018E0: btst.l #0xc on aste->flags */
    if ((aste->flags & 0x1000) != 0) {
        /* 0x00E018E6: only the FIRST longword of ANON_$UID is copied */
        log_uid[0] = ANON_$UID.high;
        /* 0x00E018F8: a zero-extended word from aote+0x2A */
        log_uid[1] = (uint32_t)(uint16_t)((aote->len_high) & 0xFFFF);
    } else {
        /* 0x00E0190E: the eight bytes at aote+0xA4 */
        log_uid[0] = aote->unknown_a4[0];
        log_uid[1] = aote->unknown_a4[1];
    }

    /* 0x00E01940 */
    NETLOG_$LOG_IT(1, log_uid,
                   aste->timestamp,
                   aste->page_count,
                   aste->seg_index,
                   0, 0,
                   (uint16_t)referenced);
}
