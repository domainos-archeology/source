/*
 * AST_$GET_ACL_ATTRIBUTES - Get ACL (Access Control List) attributes
 *
 * Retrieves the ACL-related attributes for an object.
 * Calls AST_$GET_ATTRIBUTES internally and extracts ACL fields.
 *
 * Parameters:
 *   uid - Pointer to object UID
 *   flags - Lookup flags
 *   acl - Output buffer for ACL attributes
 *   status - Status return
 *
 * Original address: 0x00e04aaa
 */

#include "ast.h"

void AST_$GET_ACL_ATTRIBUTES(uid_t *uid, uint16_t flags, void *acl, status_$t *status)
{
    uint32_t full_attrs[36];  /* Full attribute buffer (144 bytes) */
    uint32_t *out = (uint32_t *)acl;
    int i;

    /* Get full attributes */
    AST_$GET_ATTRIBUTES(uid, flags, full_attrs, status);

    /* Copy selected ACL fields to output */
    /* Output offset 0x00: First attribute word */
    out[0] = full_attrs[0];

    /* Output offset 0x04: Word from offset 0x84 */
    out[1] = full_attrs[33];

    /* Output offset 0x08: Word from offset 0x88 */
    out[2] = full_attrs[34];

    /* Output offset 0x0C: 11 uint32_t (44 bytes) from offset 0x40 */
    uint32_t *src = &full_attrs[16];
    for (i = 0; i < 11; i++) {
        out[3 + i] = src[i];
    }
}
