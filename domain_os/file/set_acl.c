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
#define status_$acl_unimplemented_call  0x0023001C

/* External AUDIT_$ENABLED flag */
extern int8_t AUDIT_$ENABLED;

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
    uint32_t acl_data[12];      /* 48 bytes for converted ACL data */
    uint32_t prot_info[2];      /* 8 bytes for protection info */
    uint32_t target_uid[2];     /* 8 bytes for target ACL UID */

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
        ACL_$CONVERT_FUNKY_ACL(&acl_high, acl_data, prot_info, target_uid, status_ret);

        if (*status_ret == status_$ok) {
            /* Call FILE_$SET_PROT with type 4 */
            FILE_$SET_PROT(file_uid, (uint16_t *)&PROT_TYPE_4,
                           acl_data, prot_info, status_ret);
            return;
        }
    }

    /* Log audit event if auditing is enabled and we didn't call SET_PROT */
    if ((int8_t)AUDIT_$ENABLED < 0) {
        FILE_$AUDIT_SET_PROT(file_uid, acl_data, prot_info, 4, *status_ret);
    }
}
