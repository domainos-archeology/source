/*
 * AST_$SET_TROUBLE - Mark an object as having trouble
 *
 * Sets the "trouble" attribute on an object identified by UID.
 * This indicates the object has some issue that needs attention.
 *
 * Original address: 0x00e071ea
 */

#include "ast.h"

void AST_$SET_TROUBLE(uid_t **uid_ptr)
{
    status_$t status;
    uint8_t attr_value[56];
    uid_t *uid;

    uid = *uid_ptr;

    /* Set trouble flag in attribute value */
    attr_value[0] = 0xFF;

    /* Set attribute 2 (trouble attribute) */
    AST_$SET_ATTRIBUTE(uid, 2, attr_value, &status);
}
