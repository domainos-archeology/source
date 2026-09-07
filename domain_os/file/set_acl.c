/*
 * FILE_$SET_ACL - Set file ACL
 *
 * Sets the access control list for a file using a "funky" ACL format.
 * Converts the ACL and calls FILE_$SET_PROT.
 *
 * Original address: 0x00E5E06A
 */

#include "file/file_internal.h"

/* Status codes */

/*
 * Constant data for protection type 4
 * Original address: 0x00E5E0FE (2 bytes of value 0x0004)
 */
static const uint16_t PROT_TYPE_4 = 4;

/*
 * FILE_$SET_ACL
 *
 * Sets file ACL using a "funky" ACL format that encodes type information
 * in the UID low word.
 *
 * Parameters:
 *   file_uid   - UID of file to modify
 *   acl_uid    - ACL UID in funky format
 *   status_ret - Output status code
 *
 * The acl_uid encodes type in bits 4-11 of the low word:
 *   0xE0 mask: 0x00 = unimplemented
 *              0x20 = directory ACL
 *              0x40 = file ACL
 *              0x80 = full ACL conversion
 *
 * Flow:
 * 1. Extract type bits from acl_uid
 * 2. If type is 0, return unimplemented error
 * 3. Convert funky ACL format via ACL_$CONVERT_FUNKY_ACL
 * 4. Call FILE_$SET_PROT with type 4
 */
void FILE_$SET_ACL(uid_t *file_uid, uid_t *acl_uid, status_$t *status_ret)
{
    uint32_t acl_high;
    uint32_t acl_low;
    uint16_t type_bits;

    /* Converted ACL components */
    uint32_t acl_data[12];      /* A6-0x40: 48 bytes of converted ACL data */
    /*
     * A6-0x10: an 8-byte UID.  FILE_$SET_PROT reads it as one
     * (`and.w (0x4,A2),D0w` at 0x00E5DF58, then eight bytes at
     * 0x00E5DF64), so it is a uid_t, not a pair of longwords.
     */
    uid_t prot_info;
    uid_t target_uid;           /* A6-0x08 */

    /* Copy ACL UID */
    acl_high = acl_uid->high;
    acl_low = acl_uid->low;

    /*
     * Extract type bits from low word.
     * The type is encoded in bits 4-11, masked with 0xFF0, then shifted right by 4.
     * We then check the high 3 bits (0xE0 mask).
     */
    type_bits = (uint16_t)((acl_low & 0xFF0) >> 4) & 0xE0;

    if (type_bits == 0) {
        /* Type 0 is unimplemented */
        *status_ret = status_$acl_unimplemented_call;
    } else {
        /* Convert the funky ACL format */
        ACL_$CONVERT_FUNKY_ACL(&acl_high, acl_data, &prot_info, &target_uid,
                               status_ret);

        if (*status_ret == status_$ok) {
            /* Call FILE_$SET_PROT with type 4 */
            FILE_$SET_PROT(file_uid, (uint16_t *)&PROT_TYPE_4,
                           acl_data, &prot_info, status_ret);
            return;
        }
    }

    /* Log audit event if auditing is enabled and we didn't call SET_PROT */
    if ((int8_t)AUDIT_$ENABLED < 0) {
        FILE_$AUDIT_SET_PROT(file_uid, acl_data, &prot_info, 4, *status_ret);
    }
}
