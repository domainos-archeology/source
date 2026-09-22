/*
 * AST_$SET_TROUBLE - DXM callback: flag an object as damaged
 *
 * Runs from the DXM unwired queue with the data cell AST_$SAVE_CLOBBERED_UID
 * registered: a pointer to the cell holding &ast_$clobbered_uid.  It sets
 * attribute 2 on that object with a TRUE byte value.  The status
 * AST_$SET_ATTRIBUTE writes to (-0x40,A6) is discarded.
 *
 * Original address: 0x00E071EA (54 bytes), A5 = 0xE1DC80 (unused here).
 * Frame: (0x8,A6) the data cell (A0), *(A0) the UID pointer (A2), (-0x38)
 * the value byte set with `st`, (-0x40) the status.  The value slot is
 * 8 bytes wide in the frame; only its first byte is written.
 */

#include "ast/ast_internal.h"

void AST_$SET_TROUBLE(uid_t **uid_ptr)
{
    status_$t status;           /* (-0x40,A6) */
    uint8_t attr_value[8];      /* (-0x38,A6) */
    uid_t *uid;                 /* A2 */

    /* 0x00E071F8..0x00E071FE */
    uid = *uid_ptr;
    attr_value[0] = 0xFF;                       /* st (-0x38,A6) */

    /* 0x00E07202..0x00E07212: AST_$SET_ATTRIBUTE(uid, 2, &value, &status) */
    AST_$SET_ATTRIBUTE(uid, 2, attr_value, &status);
}
