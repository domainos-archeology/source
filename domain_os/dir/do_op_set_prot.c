/*
 * dir_$do_op_set_prot - DO_OP handler for set protection
 *
 * Server-side handler for opcode 0x52 (DIR_OP_SET_PROTECTION) in
 * DIR_$DO_OP. Validates and converts the protection data, then calls
 * FILE_$SET_PROT to apply it to the file.
 *
 * Process:
 * 1. Enter super mode
 * 2. Open directory for write (mode 2, rights 0) - this validates the
 *    caller has access to the directory containing the file
 * 3. Check if the ACL data is in "funky" format (bits 4-11 of the
 *    second word, shifted and masked against 0xE0):
 *    a. If funky: convert via ACL_$CONVERT_FUNKY_ACL
 *    b. If standard 10-ACL: copy 44 bytes of protection data directly,
 *       and copy 8 bytes of ACL UID
 * 4. Exit super mode (temporarily)
 * 5. Call FILE_$SET_PROT to apply the protection
 * 6. Re-enter super mode
 * 7. Release handle and exit super mode
 *
 * Parameters:
 *   uid        - File UID (the file to protect)
 *   prot_data  - Protection data (44 bytes of ACL entries)
 *   acl_uid    - ACL UID pointer
 *   type_info  - Protection type (passed through to FILE_$SET_PROT)
 *                Note: In the assembly, this is at +0x14 (A6) and the
 *                address at +0x14 is passed as the type parameter
 *
 * Note: The status_ret is embedded in the type_info parameter on the
 * original m68k stack. The assembly shows status_ret at A3 = (0x16,A6)
 * and the type word at (0x14,A6).
 *
 * Original address: 0x00E5216A
 * Original size: 190 bytes
 */

#include "dir/dir_internal.h"

/* ACL_$CONVERT_FUNKY_ACL - declared in acl/acl.h */

void dir_$do_op_set_prot(uid_t *uid, void *prot_data, void *acl_uid,
                         int16_t prot_type, status_$t *status_ret)
{
    uint32_t local_handle;
    uint32_t prot_buf[12];       /* 48 bytes = 11 uint32_t + extra */
    uid_t local_acl_uid;
    uint8_t convert_buf[16];
    uint32_t *acl_words = (uint32_t *)acl_uid;

    ACL_$ENTER_SUPER();

    /* Open directory for write (mode 2, rights 0) */
    dir_$open_dir(uid, 2, 0, &local_handle, status_ret);
    if (*status_ret != status_$ok) {
        goto cleanup;
    }

    /* Check if ACL data is in "funky" format */
    {
        uint16_t *acl_shorts = (uint16_t *)acl_uid;
        uint16_t funky_check = (acl_shorts[2] & 0x0FF0) >> 4;
        funky_check &= 0xE0;

        if (funky_check != 0) {
            /* Funky ACL format - convert it */
            ACL_$CONVERT_FUNKY_ACL(acl_uid, prot_buf, &local_acl_uid,
                                    convert_buf, status_ret);
            if (*status_ret != status_$ok) {
                goto cleanup;
            }
        } else {
            /* Standard 10-ACL format - copy directly */
            {
                int16_t i;
                uint32_t *src = (uint32_t *)prot_data;
                uint32_t *dst = prot_buf;
                for (i = 10; i >= 0; i--) {
                    *dst = *src;
                    src++;
                    dst++;
                }
            }
            /* Copy ACL UID */
            {
                uint32_t *src = (uint32_t *)acl_uid;
                local_acl_uid.high = src[0];
                local_acl_uid.low = src[1];
            }
        }
    }

    /* Exit super mode temporarily for the FILE_$SET_PROT call */
    ACL_$EXIT_SUPER();

    /* Apply protection to the file */
    FILE_$SET_PROT(uid, &prot_type, prot_buf, &local_acl_uid, status_ret);

    /* Re-enter super mode for cleanup */
    ACL_$ENTER_SUPER();

cleanup:
    dir_$release_handle(&local_handle);
    ACL_$EXIT_SUPER();
}
