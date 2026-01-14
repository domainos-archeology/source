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
 * Original address: 0x00e05380
 */

#include "ast.h"
#include "proc1/proc1.h"

/* Internal function prototypes */
extern void TIME_$CLOCK(uint32_t *clock);
extern void ACL_$GET_EXSID(void *exsid, status_$t *status);
extern void FUN_00e05214(uid_t *uid, uint16_t attr_id, void *value,
                          int8_t flags, void *exsid, uint32_t *clock, status_$t *status);

void AST_$SET_ATTRIBUTE(uid_t *uid, uint16_t attr_id, void *value, status_$t *status)
{
    uid_t local_uid;
    uint32_t clock_val;
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
    FUN_00e05214(&local_uid, attr_id, value, -1, exsid_buf, &clock_val, &local_status);

    PROC1_$INHIBIT_END();

    *status = local_status;
}
