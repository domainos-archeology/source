/*
 * AST_$SET_ATTRIBUTE - Set an attribute on an object
 *
 * High-level interface to set a single attribute on an object.
 * Handles special cases like ACL attributes (0x14) that require
 * extended security ID lookup.
 *
 * Parameters:
 *   uid - Pointer to object UID
 *   attr_id - Attribute identifier
 *   value - Attribute value
 *   status - Status return
 *
 * Original address: 0x00E05380 (128 bytes).  Frame: (-0x7C) status,
 * (-0x78) the clock TIME_$CLOCK fills, (-0x70) the UID copy, (-0x68) the
 * 0x68-byte subject record.  The wait flag is `st -(SP)` = TRUE.  Verified
 * against the listing 2026-09-22.
 */

#include "ast/ast_internal.h"
#include "proc1/proc1.h"
#include "acl/acl.h"
#include "time/time.h"

void AST_$SET_ATTRIBUTE(uid_t *uid, uint16_t attr_id, void *value, status_$t *status)
{
    uid_t local_uid;
    clock_t clock_val;
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

    /* Get current time */
    TIME_$CLOCK(&clock_val);

    PROC1_$INHIBIT_BEGIN();

    /* Call internal attribute setter */
    ast_$set_attribute_internal(&local_uid, attr_id, value, -1,
                                (ast_$subject_t *)exsid_buf, &clock_val,
                                &local_status);

    PROC1_$INHIBIT_END();

    *status = local_status;
}
