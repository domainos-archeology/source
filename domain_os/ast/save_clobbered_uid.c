/*
 * AST_$SAVE_CLOBBERED_UID - Save a clobbered (corrupted) UID for later recovery
 *
 * Saves a UID that has been detected as corrupted, scheduling
 * a callback to handle the trouble condition.
 *
 * Parameters:
 *   uid - Pointer to the corrupted UID
 *
 * Original address: 0x00E07220 (82 bytes).  `pea (A5)` / `lea (0xe1dc80).l,A5`:
 * (0x490,A5) is ast_$clobbered_uid.  Frame: (-0x8) the UID copy, (-0xC)
 * the status DXM writes, (-0x10) the cell holding &ast_$clobbered_uid whose
 * ADDRESS is the callback data.  Verified against the listing 2026-09-22.
 */

#include "ast/ast_internal.h"
#include "dxm/dxm.h"

void AST_$SAVE_CLOBBERED_UID(uid_t *uid)
{
    uid_t *uid_ptr;
    status_$t status;
    uid_t local_uid;

    /* 0x00E0722C..0x00E07234: two post-increment longwords */
    local_uid.high = uid->high;
    local_uid.low = uid->low;

    /* 0x00E07238..0x00E07240: into (0x490,A5) */
    ast_$clobbered_uid.high = local_uid.high;
    ast_$clobbered_uid.low = local_uid.low;

    /* 0x00E0724E..0x00E07252 */
    uid_ptr = &ast_$clobbered_uid;

    /* Schedule callback to AST_$SET_TROUBLE */
    /*
     * 0x00E07244 pea (-0xc,A6)     -> status
     * 0x00E07248 st  -(SP)          -> check_dup = true
     * 0x00E0724A move.w #0x8,-(SP)  -> data_size = 8 (the 8-byte UID)
     */
    DXM_$ADD_CALLBACK(&DXM_$UNWIRED_Q, &PTR_AST_$SET_TROUBLE_00e07272,
                      (void **)&uid_ptr, 8, true, &status);
}
