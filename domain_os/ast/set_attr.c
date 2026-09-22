/*
 * AST_$SET_ATTR - Set an attribute on an ASTE
 *
 * Low-level interface to set an attribute, taking explicit clock value
 * and flags. Similar to AST_$SET_ATTRIBUTE but with more control.
 *
 * Parameters:
 *   uid - Pointer to object UID
 *   attr_id - Attribute identifier
 *   value - Pointer to the attribute value.  0x00E05450 forwards the
 *           longword at A6+0x0E untouched and 0x00E05320 dereferences it
 *           (`movea.l (0xe,A6),A1; move.w (A1),D0w`), so it is a pointer.
 *   flags - Operation flags
 *   clock - Pointer to the 6-byte clock_t the internal routine reads at
 *           0x00E05224/0x00E05228
 *   status - Status return
 *
 * Original address: 0x00E05400 (118 bytes; the batch list's 0x00E05450 is
 * an interior push).  Frame: (-0x74) status, (-0x70) the UID copy,
 * (-0x68) the 0x68-byte subject record ACL_$GET_EXSID fills.  `flags` is
 * read as ONE BYTE (`move.b (0x12,A6),-(SP)` at 0x00E0544C) and handed to
 * ast_$set_attribute_internal as its boolean.  Verified against the
 * listing 2026-09-22.
 */

#include "ast/ast_internal.h"
#include "proc1/proc1.h"
#include "acl/acl.h"

void AST_$SET_ATTR(uid_t *uid, int16_t attr_id, void *value,
                   uint8_t flags, clock_t *clock, status_$t *status)
{
    uid_t local_uid;
    /* ACL_$GET_EXSID fills this with an ast_$subject_t (see ast/ast.h). */
    uint8_t exsid_buf[104];
    status_$t local_status;

    /* Copy UID locally */
    local_uid.high = uid->high;
    local_uid.low = uid->low;

    /* Special handling for ACL attribute (0x14) */
    if (attr_id == 0x14) {
        ACL_$GET_EXSID(exsid_buf, status);
        if (*status != status_$ok) {
            return;
        }
    }

    PROC1_$INHIBIT_BEGIN();

    /* Call internal attribute setter */
    ast_$set_attribute_internal(&local_uid, attr_id, value,
                                (boolean)flags, (ast_$subject_t *)exsid_buf,
                                clock, &local_status);

    PROC1_$INHIBIT_END();

    *status = local_status;
}
