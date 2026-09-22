/*
 * AST_$INVALIDATE - Invalidate a run of an object's pages
 *
 * Activates the object, clips [start_page, start_page+count-1] to the
 * object's last page ((length-1) >> 10), marks the AOTE in transition and
 * runs one of the two nested procedures: ast_$invalidate_with_wait
 * (flags TRUE - installed pages are removed and freed, and a wired page
 * fails the call) or ast_$invalidate_no_wait (installed pages are kept
 * but marked to be re-read).  For a remote object whose local pass
 * succeeded the invalidation is then forwarded to the home node.
 *
 * Parameters (frame at 0x00E0662E, `link.w A6,-0x1c`):
 *   uid        (0x08,A6)  (A2)
 *   start_page (0x0C,A6)  longword
 *   count      (0x10,A6)  longword (D2)
 *   flags      (0x14,A6)  a single BOOLEAN byte (D3b)
 *   status     (0x16,A6)  (A3)
 * Locals: (-0x8) the aote+0xAC/+0xB0 pair for REM_FILE, (-0xC) a cell
 * only the nested with_wait procedure writes, (-0x10) the zero location,
 * (-0x14) aote, (-0x18) end page, (-0x1A) is_remote.  The nested
 * procedures reach (-0x14), (-0x1A), (-0xC) and (0xC) through the static
 * link; they receive them as explicit arguments here.
 *
 * Original address: 0x00E0662E (336 bytes), A5 = 0xE1DC80 (AST_ block;
 * (0x428,A5) is AST_$AST_IN_TRANS_EC).
 */

#include "ast/ast_internal.h"
#include "proc1/proc1.h"
#include "rem_file/rem_file.h"

void AST_$INVALIDATE(uid_t *uid, uint32_t start_page, uint32_t count,
                     boolean flags, status_$t *status)
{
    aote_t *aote;               /* (-0x14,A6) */
    int8_t is_remote;           /* (-0x1A,A6) */
    uint32_t location;          /* (-0x10,A6) */
    uint32_t end_page;          /* (-0x18,A6) / D1 */
    uint32_t last_page;         /* D4 */
    uint32_t net_node[2];       /* (-0x8,A6) */
    uint32_t scratch;           /* (-0xC,A6) */

    /* 0x00E0664C..0x00E06660 */
    *status = status_$ok;
    PROC1_$INHIBIT_BEGIN();
    ML_$LOCK(AST_LOCK_ID);

    /* 0x00E06662..0x00E06672 */
    aote = ast_$lookup_aote_by_uid(uid);
    if (aote == NULL) {
        /* 0x00E06674..0x00E06694: location 0, force 0 (clr.w) */
        location = 0;
        aote = ast_$force_activate_segment(uid, location, status, 0);
        if (aote == NULL) {
            /* 0x00E06696..0x00E066A4 */
            ML_$UNLOCK(AST_LOCK_ID);
            goto done;
        }
    } else {
        /* 0x00E066A8 */
        aote->flags |= AOTE_FLAG_BUSY;
    }

    /* 0x00E066AE..0x00E066B8: smi of the remote flag */
    is_remote = (aote->remote_flag < 0) ? -1 : 0;

    /* 0x00E066BC..0x00E066D4: an empty object, or a start past its last
     * page (unsigned), has nothing to invalidate */
    if (aote->length != 0 && start_page <= ((aote->length - 1) >> 10)) {
        /* 0x00E066D6..0x00E066EE: clip the end (signed compare, ble) */
        end_page = start_page + count - 1;
        last_page = (aote->length - 1) >> 10;
        if ((int32_t)end_page > (int32_t)last_page) {
            end_page = last_page;
        }

        /* 0x00E066F2..0x00E06712 */
        aote->flags |= AOTE_FLAG_IN_TRANS;
        if (flags < 0) {
            *status = ast_$invalidate_with_wait(end_page, aote, start_page,
                                                is_remote, &scratch);
        } else {
            ast_$invalidate_no_wait(end_page, aote, start_page);
        }

        /* 0x00E06714..0x00E06728 */
        aote->flags &= (uint8_t)~AOTE_FLAG_IN_TRANS;
        EC_$ADVANCE(&AST_$AST_IN_TRANS_EC);
    }

    /* 0x00E0672A..0x00E06746: aote+0xAC/+0xB0, then drop the lock */
    net_node[0] = aote->obj_loc_net;
    net_node[1] = aote->obj_loc_node;
    ML_$UNLOCK(AST_LOCK_ID);

    /* 0x00E06748..0x00E0676A: forward to the home node; `move.b D3b`
     * pushes the flag byte */
    if (is_remote < 0 && *status == status_$ok) {
        REM_FILE_$INVALIDATE((uid_t *)net_node, uid, start_page, count,
                             flags, status);
    }

done:
    /* 0x00E0676E */
    PROC1_$INHIBIT_END();
}
