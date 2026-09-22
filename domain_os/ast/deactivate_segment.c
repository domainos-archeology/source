/*
 * AST_$DEACTIVATE_SEGMENT - Deactivate and clean up one segment (ASTE)
 *
 * Flushes the segment's pages, writes its segment map back, and unlinks the
 * ASTE from its AOTE's list.
 *
 * The SAU2 link map gives this address no symbol (the AST_ segment's
 * exported names skip from AST_$ADD_ASTES at 0xE0118C to AST_$GET_DISM_SEQN
 * at 0xE01388 and on to AST_$UPDATE at 0xE016D0), so it is module-local -
 * which is why it never loads A5 for itself and reaches the AST_ module
 * block through the A5 its AST_ caller already holds (0x00E01ABE
 * `pea (0x428,A5)`).  Every one of its five callers is inside AST_.
 *
 * Original address: 0x00E01950 .. 0x00E01AD0 (386 bytes)
 */

#include "ast/ast_internal.h"

/* External references - declared in subsystem headers:
 * PROC1_$CURRENT - proc1/proc1.h
 * PROC1_$TYPE - proc1/proc1.h
 * AST_$AST_IN_TRANS_EC - ast/ast.h (macro)
 * NETLOG_$OK_TO_LOG - netlog/netlog.h
 */

/*
 * Process types that may NOT deactivate a dirty remote segment.
 *
 * 0x00E0199A `cmpi.w #0x8,D1w` / `beq` (error) and 0x00E019A0
 * `cmpi.w #0x9,D1w` / `bne` (proceed): types 8 and 9 are rejected and every
 * other type is allowed.  (source-jddw: the tree had this inverted.)
 */
#define PROC1_TYPE_NO_DEACTIVATE_A  8
#define PROC1_TYPE_NO_DEACTIVATE_B  9

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

/*
 * AST_$DEACTIVATE_SEGMENT
 *
 * Parameters (prologue 0x00E01958-0x00E01960):
 *   aste    A6+0x08 long - the ASTE to deactivate
 *   purge   A6+0x0C byte - true (negative) selects PMAP_$FLUSH mode 3
 *                          instead of 1 (0x00E019F2 `tst.b D2b` / `bpl`)
 *   keep    A6+0x0E byte - AND-ed with `purge` at 0x00E01A20; when the
 *                          result is negative the segment map write-back
 *                          step is skipped entirely
 *   status  A6+0x10 long - output status
 *
 * Both flag arguments are single BYTES, each occupying the high half of its
 * stack word: the call sites push them with `move.b Dnb,-(SP)` (0x00E01B4A
 * and 0x00E01B4C in ast_$process_aote) or `st -(SP)` / `clr.w -(SP)`
 * (0x00E05E2A/0x00E05E28 in AST_$TRUNCATE); AST_$ALLOCATE_ASTE's three sites
 * push a single `clr.l -(SP)` covering both (0x00E01F86).
 *
 * Original address: 0x00E01950
 */
void AST_$DEACTIVATE_SEGMENT(aste_t *aste, int8_t purge, int8_t keep,
                             status_$t *status)
{
    uint16_t aste_flags;
    uint32_t segmap_offset;
    uint16_t flush_mode;
    uint32_t *segmap_row;
    aote_t *aote;

    /* 0x00E01968: in transition already? */
    if ((int16_t)aste->flags < 0) {
        goto not_deactivatable;
    }

    /* 0x00E0196E: still wired/referenced? */
    if (aste->wire_count != 0) {
        goto not_deactivatable;
    }

    /* 0x00E01974-0x00E01986: `sne`/`sne`/`and.b`/`bpl` - both bits set? */
    aste_flags = aste->flags;
    if ((aste_flags & ASTE_FLAG_DIRTY) != 0 &&
        (aste_flags & ASTE_FLAG_REMOTE) != 0) {
        /*
         * 0x00E01988-0x00E019A4.  PROC1_$TYPE is the word array based at
         * 0xE2612A, indexed by PROC1_$CURRENT (`(-0x2,A1,D0w)` with
         * A1 = 0xE2612C and D0 = current*2).  Types 8 and 9 are refused;
         * anything else falls through to the deactivation.
         */
        uint16_t proc_type = PROC1_$TYPE[PROC1_$CURRENT];
        if (proc_type == PROC1_TYPE_NO_DEACTIVATE_A ||
            proc_type == PROC1_TYPE_NO_DEACTIVATE_B) {
            goto not_deactivatable;
        }
    }

    /* 0x00E019B0: claim the in-transition bit */
    aste->flags |= ASTE_FLAG_IN_TRANS;

    /*
     * 0x00E019B6-0x00E019C8: A3 = 0xED5000 + seg_index*0x80, and every use
     * below is `(-0x80,A3)`, i.e. the row base 0xED4F80 + seg_index*0x80.
     */
    segmap_offset = (uint32_t)aste->seg_index * 0x80;
    segmap_row = (uint32_t *)((char *)SEGMAP_BASE + segmap_offset - 0x80);

    /* 0x00E019CC-0x00E019DC */
    if (NETLOG_$OK_TO_LOG < 0) {
        ast_$deactivate_segment_log(aste, segmap_row);
    }

    /* 0x00E019DE: release the AST lock across the I/O */
    ML_$UNLOCK(AST_LOCK_ID);

    /* 0x00E019EC-0x00E019F6 */
    flush_mode = 1;
    if (purge < 0) {
        flush_mode = 3;
    }

    /*
     * 0x00E019FC-0x00E01A16.  Six Pascal arguments, but the assembler emits
     * only five pushes: `pea (0x20).w` at 0x00E01A04 supplies BOTH constant
     * word arguments at once, the 0 that lands at A6+0x10 (start_page) and
     * the 0x20 at A6+0x12 (count).  PMAP_$FLUSH itself reads them as two
     * separate words (0x00E137BC `move.w (0x10,A6),D4w` and 0x00E1377A
     * `move.w (0x12,A6),D5w`), and its other caller at 0x00E057E6 pushes
     * them as two `move.w`s, so this really is a six-argument call.
     */
    PMAP_$FLUSH(aste, segmap_row, 0, 0x20, flush_mode, status);

    /* 0x00E01A1A */
    if (*status != status_$ok) {
        goto error_exit;
    }

    /* 0x00E01A20-0x00E01A24: `move.b D2b,D0b` / `and.b D3b,D0b` / `bmi` */
    if ((int8_t)(purge & keep) >= 0) {
        /* 0x00E01A2A: the flags word is re-read here, not cached */
        if ((aste->flags & ASTE_FLAG_AREA) != 0) {
            /* 0x00E01A34-0x00E01A3E */
            AREA_$DEACTIVATE_ASTE(aste, status);
        } else {
            /* 0x00E01A42-0x00E01A52 */
            ast_$update_aste(aste, (segmap_entry_t *)segmap_row, 0, status);
        }

        /* 0x00E01A56 */
        if (*status != status_$ok) {
            goto error_exit;
        }
    }

    /* 0x00E01A5A */
    ML_$LOCK(AST_LOCK_ID);

    /* 0x00E01A6C: re-read again */
    if ((aste->flags & ASTE_FLAG_AREA) == 0) {
        /* 0x00E01A76-0x00E01A9A: unlink the ASTE from its AOTE's list */
        aote = aste->aote;
        if (aote->aste_list == aste) {
            aote->aste_list = aste->next;
        } else {
            aste_t *prev = aote->aste_list;
            while (prev->next != aste) {
                prev = prev->next;
            }
            prev->next = aste->next;
        }

        /* 0x00E01A9C */
        aote->status_flags--;
    }

    return;

error_exit:
    /*
     * 0x00E01AA2 `bset.b #0x7,(A2)` sets bit 7 of the FIRST byte of the
     * status longword, i.e. bit 31 of the value - the "this is a warning"
     * marker.  A byte-pointer store would pick the wrong end on a
     * little-endian host.
     */
    *status = (status_$t)((uint32_t)*status | 0x80000000u);

    /* 0x00E01AA6-0x00E01AC6 */
    ML_$LOCK(AST_LOCK_ID);
    aste->flags &= (uint16_t)~ASTE_FLAG_IN_TRANS;
    EC_$ADVANCE(&AST_$AST_IN_TRANS_EC);
    return;

not_deactivatable:
    /* 0x00E019A6 */
    *status = status_$ast_segment_not_deactivatable;
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
    if ((aste->flags & ASTE_FLAG_AREA) != 0) {
        /* 0x00E018E6: only the FIRST longword of ANON_$UID is copied */
        log_uid[0] = ANON_$UID.high;
        /* 0x00E018F8: a zero-extended word from aote+0x2A */
        log_uid[1] = (uint32_t)(uint16_t)((aote->dtm_high) & 0xFFFF);
    } else {
        /* 0x00E0190A-0x00E01912: the eight bytes at aote+0xA4 */
        log_uid[0] = aote->obj_loc_uid.high;
        log_uid[1] = aote->obj_loc_uid.low;
    }

    /* 0x00E01940 */
    NETLOG_$LOG_IT(1, log_uid,
                   aste->segment,
                   aste->page_count,
                   aste->seg_index,
                   0, 0,
                   (uint16_t)referenced);
}
