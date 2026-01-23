/*
 * FILE_$SET_PROT - Set file protection
 *
 * Main function for setting file protection attributes.
 * Maps protection types to attribute IDs and calls FILE_$SET_PROT_INT.
 *
 * Original address: 0x00E5DF3A
 */

#include "file/file_internal.h"

/* Status codes */
#define status_$file_invalid_arg    0x000F0014

/*
 * Protection type to attribute ID mapping:
 *   0 -> 0x10 (Set protection mode 0)
 *   1 -> 0x11 (Set protection mode 1)
 *   2 -> 0x12 (Set protection mode 2)
 *   3 -> 0x15 (Set protection mode 3)
 *   4 -> 0x13 (Set protection mode 4)
 *   5 -> 0x14 (Set protection mode 5)
 *   6 -> 0x03 (Set protection by ACL UID)
 *   7+ -> Invalid
 */

/*
 * FILE_$SET_PROT
 *
 * Sets file protection based on protection type.
 *
 * Parameters:
 *   file_uid   - UID of file to modify
 *   prot_type  - Pointer to protection type (0-6)
 *   acl_data   - ACL data buffer (44 bytes)
 *   acl_uid    - ACL UID (8 bytes) with flags encoded in low word
 *   status_ret - Output status code
 *
 * The acl_uid parameter encodes special flags in the low word:
 *   Bits 4-11 (masked with 0xFF0 >> 4): If bit 4 is set, use default protection
 *
 * Flow:
 * 1. Check if default protection mode is requested
 * 2. If so, get default protection and potentially use type 6
 * 3. Map protection type to attribute ID
 * 4. Call FILE_$SET_PROT_INT to do the actual work
 *
 * Stack layout (-0x4C bytes):
 *   -0x4A: attr_id (2 bytes)
 *   -0x46: default_prot (2 bytes) - output from nested proc
 *   -0x44: local_status (4 bytes) - written by nested proc
 *   -0x40: local_acl[11] (44 bytes) - ACL data buffer
 *   -0x14: local_uid (8 bytes) - high at -0x14, low at -0x10
 *   -0x08: extra_data (8 bytes) - for ACL_$DEF_ACLDATA
 */
void FILE_$SET_PROT(uid_t *file_uid, uint16_t *prot_type, uint32_t *acl_data,
                    uid_t *acl_uid, status_$t *status_ret)
{
    uint16_t type_val;
    uint16_t attr_id;
    int16_t i;
    uint32_t *src;
    uint32_t *dst;

    /* Local copies - matching stack layout */
    uint32_t local_acl[11];     /* 44 bytes for ACL data at -0x40(A6) */
    uint8_t extra_data[8];      /* Extra data buffer at -0x8(A6) */
    uint32_t local_uid_high;    /* Local UID high at -0x14(A6) */
    uint32_t local_uid_low;     /* Local UID low at -0x10(A6) */
    int16_t default_prot;       /* Default protection at -0x46(A6) */
    status_$t local_status;     /* Local status at -0x44(A6) */

    type_val = *prot_type;

    /*
     * Check if default protection mode is requested.
     * Flag is in bits 4-11 of low word of acl_uid->low, bit 4 set indicates default mode.
     * Assembly: and.w (0x4,A2),D0w with #0xff0, then lsr.w #4, then btst #4
     */
    if ((((acl_uid->low & 0xFF0) >> 4) & 0x10) != 0) {
        /*
         * Default protection mode - need to get current protection
         * and potentially switch to type 6 if none exists.
         */
        local_uid_high = acl_uid->high;
        local_uid_low = acl_uid->low & 0xF0FFFFFF;  /* Clear upper nibble: andi.b #-0x10,(-0x10,A6) */

        /*
         * Inlined FILE_$GET_DEFAULT_PROT (originally at 0x00E5DEF4)
         *
         * This was a Pascal nested procedure that:
         * 1. Gets file_uid from parent frame
         * 2. Calls AST_$GET_COMMON_ATTRIBUTES with mode 1
         * 3. Writes status to parent's local_status (-0x44)
         * 4. Returns protection value (first byte of attr buffer)
         *
         * We inline it here since C doesn't support nested procedures
         * that access parent stack frames.
         */
        {
            uint8_t attr_buf[24];   /* Common attributes buffer */
            uid_t lookup_uid;       /* UID for lookup */

            /* Copy file UID for lookup */
            lookup_uid.high = file_uid->high;
            lookup_uid.low = file_uid->low;

            /* Note: Original clears bit 6 of a flags byte at -0xb offset
             * This is related to remote flag handling in the lookup context */

            /* Get common attributes (mode 1 = get protection info) */
            AST_$GET_COMMON_ATTRIBUTES(&lookup_uid, 1, attr_buf, &local_status);

            /* Return protection value (first byte of attr_buf) */
            default_prot = (int16_t)(uint8_t)attr_buf[0];
        }

        *status_ret = local_status;
        if (local_status != status_$ok) {
            return;
        }

        if (default_prot == 0) {
            /* No protection set - use type 6 (ACL by UID) */
            type_val = 6;

            /* Get default ACL data */
            ACL_$DEF_ACLDATA(local_acl, extra_data);
            goto set_protection;
        }

        /* Invalid type - fall through to error */
        goto invalid_arg;
    }

    /* Copy ACL data (11 uint32_t = 44 bytes) using dbf loop */
    src = acl_data;
    dst = local_acl;
    for (i = 10; i >= 0; i--) {
        *dst++ = *src++;
    }

    /* Copy ACL UID (8 bytes) */
    local_uid_high = acl_uid->high;
    local_uid_low = acl_uid->low;

set_protection:
    /* Map protection type to attribute ID via switch/jump table */
    if (type_val >= 7) {
        goto invalid_arg;
    }

    switch (type_val) {
    case 0:
        attr_id = 0x10;
        break;
    case 1:
        attr_id = 0x11;
        break;
    case 2:
        attr_id = 0x12;
        break;
    case 3:
        attr_id = 0x15;
        break;
    case 4:
        attr_id = 0x13;
        break;
    case 5:
        attr_id = 0x14;
        break;
    case 6:
        /* Type 6: Set ACL by UID - use the UID directly as first ACL entry */
        attr_id = 0x03;
        local_acl[0] = local_uid_high;
        local_acl[1] = local_uid_low;
        break;
    default:
        goto invalid_arg;
    }

    /* Call internal set protection function */
    FILE_$SET_PROT_INT(file_uid, local_acl, attr_id, type_val, 0, status_ret);
    return;

invalid_arg:
    *status_ret = status_$file_invalid_arg;

    /* Log audit event if auditing is enabled (tst.b checks high bit) */
    if ((int8_t)AUDIT_$ENABLED < 0) {
        FILE_$AUDIT_SET_PROT(file_uid, acl_data, acl_uid, *prot_type, *status_ret);
    }
}
