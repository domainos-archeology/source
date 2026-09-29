/*
 * AST_$DISMOUNT - Deactivate every object on a volume and dismount its VTOC
 *
 * Marks the volume as dismounting in AST_$DATA.vol_info_count, bumps the
 * dismount sequence number, waits until no AOTE activation is in progress
 * on the volume (AST_$DATA.vol_indices[vol] == 0, watched through AST_$DISM_EC),
 * then walks the whole AOT: every local AOTE on this volume whose UID is
 * not the paging file's is processed with ast_$process_aote(purge = flags,
 * keep = TRUE, wait = TRUE) and released.  A processing failure records
 * the AOTE in AST_$DISMOUNT_FAILED_PTR and skips VTOC_$DISMOUNT.
 *
 * Parameters (frame at 0x00E069CA, `link.w A6,-0xc`):
 *   vol_index (0x8,A6)  word (D2)
 *   flags     (0xA,A6)  a single byte: the `purge` flag handed to
 *                       ast_$process_aote and VTOC_$DISMOUNT
 *   status    (0xC,A6)
 * Locals: (-0x4,A6) status, (-0xC,A6) the EC value to wait for.
 *
 * Original address: 0x00E069CA (340 bytes), A5 = 0xE1DC80 (AST_ block):
 *   (0x3F4,A5) AST_$AOTE_LIMIT   (0x404,A5) AST_$DISM_SEQN
 *   (0x408,A5) AST_$DISM_EC      (0x412,A5) AST_$DATA.vol_indices (word array)
 *   (0x420,A5) AST_$DATA.vol_info_count   (0x438,A5) AST_$DISMOUNT_FAILED_PTR
 */

#include "ast/ast_internal.h"
#include "proc1/proc1.h"
#include "vtoc/vtoc.h"
#include "network/network.h"

/* The first AOTE, `AOT` in the SAU2 map (`movea.l #0xec7b60,A2`), is
 * AST_$AOT.aote[0] (ast/ast.h). */

void AST_$DISMOUNT(uint16_t vol_index, uint8_t flags, status_$t *status)
{
    status_$t local_status;     /* (-0x4,A6) */
    uint16_t vol_mask;          /* D3w */
    int32_t wait_value;         /* (-0xC,A6) */
    aote_t *aote;               /* A2 */

    /* 0x00E069D8..0x00E069F2 */
    local_status = status_$ok;
    PROC1_$INHIBIT_BEGIN();
    ML_$LOCK(AST_LOCK_ID);

    /* 0x00E069F4..0x00E069FC: clr.w D3 / bset.l D2,D3 (mod 32) / or.w */
    vol_mask = (uint16_t)(1u << (vol_index & 0x1F));
    AST_$DATA.vol_info_count |= vol_mask;
    AST_$DISM_SEQN++;

    /*
     * 0x00E06A00..0x00E06A56: `bra` to the test first; while the volume's
     * activation count is non-zero, wait for AST_$DISM_EC to pass its
     * current value.
     */
    while (AST_$DATA.vol_indices[vol_index] != 0) {
        wait_value = AST_$DISM_EC.value + 1;                /* 0x00E06A12 */
        ML_$UNLOCK(AST_LOCK_ID);
        /*
         * 0x00E06A2A..0x00E06A40: both three-element arrays by value, 24
         * bytes: { &AST_$DISM_EC, NULL, NULL } (A2 is zero from 0x00E06A02)
         * and { wait_value, 0, 0 }.  The returned index is discarded.
         */
        EC_$WAIT((ec_$wait_ecs_t){{ &AST_$DISM_EC, NULL, NULL }},
                 (ec_$wait_vals_t){{ wait_value, 0, 0 }});
        ML_$LOCK(AST_LOCK_ID);
    }

    /* 0x00E06A58..0x00E06ADC: walk the AOT up to AST_$AOTE_LIMIT (`bne`) */
    aote = &AST_$AOT.aote[0];
    while (aote != AST_$AOTE_LIMIT) {
        /* 0x00E06A60: remote objects are not ours */
        if (aote->remote_flag < 0) {
            goto next;
        }
        /* 0x00E06A66..0x00E06A6E: byte volume index vs the word argument */
        if ((uint16_t)aote->vol_index != vol_index) {
            goto next;
        }
        /* 0x00E06A70..0x00E06A7A: in transition - wait and re-test the
         * SAME entry (bra 0x00E06AD8 without advancing A2) */
        if ((int8_t)aote->flags < 0) {
            AST_$WAIT_FOR_AST_INTRANS();
            continue;
        }
        /* 0x00E06A7C..0x00E06A84: first byte of the UID zero = unused */
        if ((aote->uid.high & 0xFF000000u) == 0) {
            goto next;
        }
        /* 0x00E06A86..0x00E06A98: the paging file is never dismounted */
        if (aote->uid.high == NETWORK_$PAGING_FILE_UID.high &&
            aote->uid.low == NETWORK_$PAGING_FILE_UID.low) {
            goto next;
        }

        /*
         * 0x00E06A9A..0x00E06AAE: `st -(SP)` (wait), `st -(SP)` (keep),
         * `move.b (0xa,A6),-(SP)` (purge) - three single bytes.
         */
        ast_$process_aote(aote, (boolean)flags, -1, -1, &local_status);
        if (local_status != status_$ok) {
            /* 0x00E06AB8..0x00E06ACA */
            ML_$UNLOCK(AST_LOCK_ID);
            AST_$DISMOUNT_FAILED_PTR = aote;
            goto done;
        }
        /* 0x00E06ACC..0x00E06AD2 */
        ast_$release_aote(aote);

next:
        /* 0x00E06AD4: lea (0xc0,A2),A2 */
        aote = aote + 1;
    }

    /* 0x00E06ADE..0x00E06AFC */
    ML_$UNLOCK(AST_LOCK_ID);
    VTOC_$DISMOUNT(vol_index, flags, &local_status);

done:
    /* 0x00E06AFE..0x00E06B10 */
    AST_$DATA.vol_info_count &= (uint16_t)~vol_mask;
    PROC1_$INHIBIT_END();
    *status = local_status;
}
