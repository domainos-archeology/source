/*
 * AST_$GET_ACL_ATTRIBUTES - Get ACL (Access Control List) attributes
 *
 * Retrieves the ACL-related attributes for an object.
 * Calls AST_$GET_ATTRIBUTES internally and extracts ACL fields.
 *
 * Parameters:
 *   loc_rec - The 0x20-byte object-location record (UID at +0x08), passed
 *             straight through to AST_$GET_ATTRIBUTES (0x00E04ACA)
 *   flags   - Lookup flags
 *   acl     - Output record (0x38 bytes)
 *   status  - Status return
 *
 * Original address: 0x00e04aaa
 */

#include "ast/ast_internal.h"

void AST_$GET_ACL_ATTRIBUTES(file_$obj_loc_t *loc_rec, uint16_t flags,
                             ast_$acl_attr_t *acl, status_$t *status)
{
    /* A6-0x90: the private 0x90-byte attribute record */
    uint32_t full_attrs[AST_ATTR_REC_SIZE / 4];
    uint32_t *out = (uint32_t *)(void *)acl;
    int i;

    /*
     * 0x00E04ACA passes the caller's record pointer unchanged:
     * AST_$GET_ATTRIBUTES' first argument is the 0x20-byte object-location
     * record, not a bare UID (see the note in file/file.h).
     */
    AST_$GET_ATTRIBUTES(loc_rec, flags, full_attrs, status);

    /* 0x00E04AD6: out[0x00] <- attrs[0x00] */
    out[0] = full_attrs[0];

    /*
     * 0x00E04ADA: `lea (-0x8,A6),A0` = the record base (A6-0x90) + 0x88,
     * so out[0x04] and out[0x08] come from attrs[0x88] and attrs[0x8C].
     */
    out[1] = full_attrs[0x88 / 4];
    out[2] = full_attrs[0x8C / 4];

    /*
     * 0x00E04AE6: `lea (-0x48,A6),A0` = the record base + 0x48; 11
     * longwords (`moveq #0xa` + dbf) land at out+0x0C.
     */
    {
        uint32_t *src = &full_attrs[0x48 / 4];
        for (i = 0; i < 11; i++) {
            out[3 + i] = src[i];
        }
    }
}
