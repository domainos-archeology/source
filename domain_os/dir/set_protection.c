/*
 * DIR_$SET_PROTECTION - Set protection on a file
 *
 * Sets the ACL/protection on a specific file.
 *
 * Original address: 0x00E52228
 * Original size: 362 bytes
 */

#include "dir/dir_internal.h"

/*
 * DIR_$SET_PROTECTION - Set protection on a file
 *
 * Sets the protection/ACL on a file. For certain protection types,
 * this involves locking the file, setting the protection, and
 * unlocking.
 *
 * Parameters:
 *   file_uid   - UID of file
 *   prot_buf   - Protection data to set (44 bytes)
 *   acl_uid    - ACL UID
 *   prot_type  - Pointer to protection type (4, 5, or 6)
 *   status_ret - Output: status code
 */
void DIR_$SET_PROTECTION(uid_t *file_uid, void *prot_buf, uid_t *acl_uid,
                         int16_t *prot_type, status_$t *status_ret)
{
    struct {
        uint8_t   op;
        uint8_t   padding[3];
        uid_t     uid;          /* File UID */
        uint16_t  reserved;
        uint8_t   gap[0x80];
        uint32_t  prot[11];     /* Protection data (44 bytes) */
        uid_t     acl;          /* ACL UID */
        int16_t   type;         /* Protection type */
    } request;
    struct {
        uint8_t   flags[8];
        uid_t     temp_acl;
    } response;
    status_$t status;
    uint32_t *src, *dst;
    int16_t i, type;
    uint32_t lock_handle;
    uint16_t lock_result;
    uint32_t dtv_buf[2];
    status_$t lock_status;

    /* Build the request */
    request.op = DIR_OP_SET_PROTECTION;
    request.uid.high = file_uid->high;
    request.uid.low = file_uid->low;
    request.reserved = DAT_00e7fce2;

    /* Copy protection data into request */
    src = (uint32_t *)prot_buf;
    dst = request.prot;
    for (i = 0; i < 11; i++) {
        *dst++ = *src++;
    }

    /* Copy ACL UID and type */
    request.acl.high = acl_uid->high;
    request.acl.low = acl_uid->low;
    request.type = *prot_type;

    /* Send the request */
    DIR_$DO_OP(&request.op, DAT_00e7fce6, 0x14,
               (Dir_$OpResponse *)response.flags, &request);

    status = *((status_$t *)&response.flags[4]);

    /* Check for fallback conditions */
    if (status == file_$bad_reply_received_from_remote_node ||
        status == status_$naming_bad_directory) {
        type = *prot_type;

        /* Only handle types 4, 5, 6 */
        if (type != 4 && type != 5 && type != 6) {
            *status_ret = file_$incompatible_request;
            return;
        }

        /* Check if ACL has the "funky" bit set (bit 4 of byte 5) */
        if (((acl_uid->low >> 4) & 0x10) == 0) {
            /* Convert to 9ACL format */
            ACL_$CONVERT_TO_9ACL((int16_t)(uintptr_t)prot_buf, acl_uid, file_uid,
                                 &ACL_$DIR_ACL, &response.temp_acl, status_ret);
            if (*status_ret != status_$ok) {
                return;
            }
        } else {
            /* Use ACL directly */
            response.temp_acl.high = acl_uid->high;
            response.temp_acl.low = acl_uid->low;
        }

        /* Lock the file for protection update */
        FILE_$PRIV_LOCK(file_uid, PROC1_$AS_ID, 0, 4, 0, 0x80000, 0, 0, 0,
                        NULL, 1, &lock_handle, &lock_result, status_ret);
        if (*status_ret != status_$ok) {
            return;
        }

        /* Set the protection */
        FILE_$SET_PROT(file_uid, NULL, prot_buf, &response.temp_acl, status_ret);

        /* Unlock the file */
        FILE_$PRIV_UNLOCK(file_uid, (uint16_t)lock_handle, ((uint32_t)4 << 16) | PROC1_$AS_ID,
                          0, 0, 0, dtv_buf, &lock_status);
    } else {
        *status_ret = status;
    }
}
