/*
 * AST_$SET_ATTR - Set an attribute on an ASTE
 *
 * Low-level interface to set an attribute, taking explicit clock value
 * and flags. Similar to AST_$SET_ATTRIBUTE but with more control.
 *
 * Parameters:
 *   uid - Pointer to object UID
 *   attr_id - Attribute identifier
 *   value - Attribute value
 *   flags - Operation flags
 *   clock - Timestamp for the operation
 *   status - Status return
 *
 * Original address: 0x00e05400
 */

#include "ast/ast_internal.h"
#include "proc1/proc1.h"
#include "acl/acl.h"

void AST_$SET_ATTR(uid_t *uid, int16_t attr_id, uint32_t value,
                   uint8_t flags, uint32_t *clock, status_$t *status)
{
    uid_t local_uid;
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
    FUN_00e05214(&local_uid, attr_id, value, flags, exsid_buf, clock, &local_status);

    PROC1_$INHIBIT_END();

    *status = local_status;
}
