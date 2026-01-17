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

/* External AUDIT_$ENABLED flag */
extern int8_t AUDIT_$ENABLED;

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
static const uint16_t prot_type_to_attr_id[] = {
    0x10,  /* Type 0 */
    0x11,  /* Type 1 */
    0x12,  /* Type 2 */
    0x15,  /* Type 3 */
    0x13,  /* Type 4 */
    0x14,  /* Type 5 */
    0x03,  /* Type 6 - uses ACL UID directly */
};

/*
 * FILE_$GET_DEFAULT_PROT - Get default protection for file (nested procedure)
 *
 * This is a Pascal nested procedure that accesses the parent stack frame.
 * It gets the default protection for a file using AST_$GET_COMMON_ATTRIBUTES.
 *
 * Original address: 0x00E5DEF4
 *
 * Parameters (from parent frame):
 *   prot_out - Output for protection value
 *
 * Uses parent frame to access:
 *   - File UID
 *   - Lookup context with remote flags
 */
static void FILE_$GET_DEFAULT_PROT(uid_t *file_uid, status_$t *status_ret,
                                    uint16_t *prot_out)
{
    struct {
        uint32_t uid_high;
        uint32_t uid_low;
        uint8_t  pad[5];
        uint8_t  remote_flags;
    } lookup_context;

    uint8_t attr_buf[24];   /* Common attributes buffer */

    /* Copy file UID */
    lookup_context.uid_high = file_uid->high;
    lookup_context.uid_low = file_uid->low;

    /* Clear remote flag */
    lookup_context.remote_flags &= ~0x40;

    /* Get common attributes (mode 1 = get protection) */
    AST_$GET_COMMON_ATTRIBUTES(file_uid, 1, attr_buf, status_ret);

    /* Return protection value (first byte of attr_buf) */
    *prot_out = (uint16_t)attr_buf[0];
}

/*
 * FILE_$SET_PROT
 *
 * Sets file protection based on protection type.
 *
 * Parameters:
 *   file_uid   - UID of file to modify
 *   prot_type  - Pointer to protection type (0-6)
 *   acl_data   - ACL data buffer (44 bytes)
 *   acl_uid    - ACL UID (8 bytes)
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
 */
void FILE_$SET_PROT(uid_t *file_uid, uint16_t *prot_type, uint32_t *acl_data,
                    uint32_t *acl_uid, status_$t *status_ret)
{
    uint16_t type_val = *prot_type;
    uint16_t attr_id;
    int16_t i;
    uint32_t *src;
    uint32_t *dst;
    status_$t local_status;

    /* Local copies */
    uint32_t local_acl[11];     /* 44 bytes for ACL data */
    uint32_t local_uid[2];      /* 8 bytes for ACL UID */
    uint16_t default_prot;

    /*
     * Check if default protection mode is requested.
     * Flag is in bits 4-11 of low word of acl_uid[1], bit 4 set indicates default mode.
     */
    uint16_t flag_bits = (uint16_t)((acl_uid[1] & 0xFF0) >> 4);

    if (flag_bits & 0x10) {
        /*
         * Default protection mode - need to get current protection
         * and potentially switch to type 6 if none exists.
         */
        local_uid[0] = acl_uid[0];
        local_uid[1] = acl_uid[1] & 0xF0FFFFFF;  /* Clear type nibble */

        /* Get default protection */
        FILE_$GET_DEFAULT_PROT(file_uid, &local_status, &default_prot);

        *status_ret = local_status;
        if (local_status != status_$ok) {
            return;
        }

        if (default_prot == 0) {
            /* No protection set - use type 6 (ACL by UID) */
            type_val = 6;

            /* Get default ACL data */
            ACL_$DEF_ACLDATA(local_acl, local_uid);
            goto set_protection;
        }

        /* Invalid type - fall through to error */
        goto invalid_arg;
    }

    /* Copy ACL data (11 uint32_t = 44 bytes) */
    src = acl_data;
    dst = local_acl;
    for (i = 10; i >= 0; i--) {
        *dst++ = *src++;
    }

    /* Copy ACL UID (2 uint32_t = 8 bytes) */
    local_uid[0] = acl_uid[0];
    local_uid[1] = acl_uid[1];

set_protection:
    /* Map protection type to attribute ID */
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
        local_acl[0] = local_uid[0];
        local_acl[1] = local_uid[1];
        break;
    default:
        goto invalid_arg;
    }

    /* Call internal set protection function */
    FILE_$SET_PROT_INT(file_uid, local_acl, attr_id, type_val, 0, status_ret);
    return;

invalid_arg:
    *status_ret = status_$file_invalid_arg;

    /* Log audit event if auditing is enabled */
    if ((int8_t)AUDIT_$ENABLED < 0) {
        FILE_$AUDIT_SET_PROT(file_uid, acl_data, acl_uid, *prot_type, *status_ret);
    }
}
