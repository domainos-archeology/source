/*
 * AST_$SAVE_CLOBBERED_UID - Save a clobbered (corrupted) UID for later recovery
 *
 * Saves a UID that has been detected as corrupted, scheduling
 * a callback to handle the trouble condition.
 *
 * Parameters:
 *   uid - Pointer to the corrupted UID
 *
 * Original address: 0x00e07220
 */

#include "ast.h"

/* Internal function prototypes */
extern void DXM_$ADD_CALLBACK(void *queue, void *callback, void *param,
                               uint32_t flags, status_$t *status);

/* External data */
extern void DXM_$UNWIRED_Q;
extern void *PTR_AST_$SET_TROUBLE_00e07272;
extern uid_t DAT_00e1e110;  /* AST globals offset 0x490 */

void AST_$SAVE_CLOBBERED_UID(uid_t *uid)
{
    uid_t *uid_ptr;
    status_$t status;
    uid_t local_uid;

    /* Copy UID locally */
    local_uid.high = uid->high;
    local_uid.low = uid->low;

    /* Save to global storage */
    DAT_00e1e110.high = local_uid.high;
    DAT_00e1e110.low = local_uid.low;

    /* Set up pointer for callback */
    uid_ptr = &DAT_00e1e110;

    /* Schedule callback to AST_$SET_TROUBLE */
    DXM_$ADD_CALLBACK(&DXM_$UNWIRED_Q, &PTR_AST_$SET_TROUBLE_00e07272,
                      &uid_ptr, 0xFF08, &status);
}
