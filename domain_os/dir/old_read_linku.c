/*
 * DIR_$OLD_READ_LINKU - Legacy read symbolic link
 *
 * Reads the target of a symbolic link from a directory entry.
 * Sets output uid to NIL, validates the leaf name, enters super mode,
 * finds the entry, and reads the link data.
 *
 * Original address: 0x00E577F4
 * Original size: 304 bytes
 */

#include "dir/dir_internal.h"

/*
 * DIR_$OLD_READ_LINKU - Legacy read symbolic link
 *
 * The process is:
 * 1. Set target_uid to UID_$NIL
 * 2. Validate the leaf name
 * 3. Enter super mode / acquire directory lock
 * 4. Find the entry by name
 * 5. Read the link type from the entry
 *    - Type 1: direct UID (copy from entry)
 *    - Type 3: text link (read via FUN_00e55764 into local buf,
 *              then UNMAP_CASE to caller's buffer)
 * 6. Release lock and exit super mode
 *
 * Parameters:
 *   dir_uid_low  - Low part of directory UID pointer (legacy calling convention)
 *   name_low     - Low part of name pointer (legacy calling convention)
 *   name_len     - Pointer to name length
 *   target_low   - Low part of target buffer pointer
 *   target_len   - Pointer to target buffer length
 *   target_uid   - Output: target UID
 *   status_ret   - Output: status code
 *
 * Note: The legacy calling convention passes some params as split addresses.
 * In this implementation we treat them as full pointers since we're on a
 * flat address space.
 */
void DIR_$OLD_READ_LINKU(int16_t dir_uid_low, int16_t name_low, uint16_t *name_len,
                         int16_t target_low, int16_t *target_len,
                         uid_t *target_uid, status_$t *status_ret)
{
    uid_t *dir_uid = (uid_t *)(uintptr_t)dir_uid_low;
    char *name = (char *)(uintptr_t)name_low;
    char *target = (char *)(uintptr_t)target_low;
    uint8_t parsed_name[32];
    uint16_t parsed_len;
    char local_buf[256];
    uint16_t local_buf_len;
    uint32_t handle;
    int32_t entry;
    uint16_t param5, param6;
    int8_t valid;
    int8_t found;
    int8_t truncated;
    int16_t max_out_len;
    int16_t out_len;
    status_$t local_status;

    /* Set target_uid to NIL */
    target_uid->high = UID_$NIL.high;
    target_uid->low = UID_$NIL.low;

    /* Validate and parse the leaf name */
    valid = FUN_00e54414(name, *name_len, parsed_name, &parsed_len);
    if (valid >= 0) {
        *status_ret = status_$naming_invalid_leaf;
        return;
    }

    /* Enter super mode / acquire directory lock */
    FUN_00e54854(dir_uid, &handle, 0x10004, status_ret);
    if ((int16_t)*status_ret != 0) {
        ACL_$EXIT_SUPER();
        return;
    }

    /* Find the entry by name */
    found = FUN_00e54b9e(handle, parsed_name, parsed_len,
                         &entry, &param5, &param6);
    if (found >= 0) {
        /* Entry not found */
        *status_ret = status_$naming_name_not_found;
    } else {
        /* Read link type from entry at offset 0x27 */
        uint8_t link_type = *((uint8_t *)(entry + 0x27));

        if (link_type == 1) {
            /* Type 1: direct UID - copy from entry at offset 0x28 */
            target_uid->high = *((uint32_t *)(entry + 0x28));
            target_uid->low = *((uint32_t *)(entry + 0x2c));
            /* TODO: Ghidra shows status 0xe0006, verify this status code */
            *status_ret = 0x000E0006;
        } else if (link_type == 3) {
            /* Type 3: text link - read via FUN_00e55764 into local buffer */
            FUN_00e55764(handle, entry + 0x28, (uint8_t *)local_buf,
                         &local_buf_len, status_ret);
            /* Unmap case from local buffer to caller's target buffer */
            max_out_len = 0x0100;  /* 256 */
            UNMAP_CASE(local_buf, (int16_t *)&local_buf_len,
                       target, &max_out_len, &out_len,
                       (uint8_t *)&truncated);
            if (truncated < 0) {
                *status_ret = status_$naming_invalid_link;
            }
        }
        /* link_type == 0: no action (status remains ok) */
    }

    /* Release directory lock */
    FUN_00e54734(&local_status);
    if ((int16_t)*status_ret == 0) {
        *status_ret = local_status;
    }

    ACL_$EXIT_SUPER();
}
