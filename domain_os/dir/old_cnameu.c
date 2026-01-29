/*
 * DIR_$OLD_CNAMEU - Legacy change name (rename) entry
 *
 * Renames a directory entry from old_name to new_name.
 * Validates both names, enters super mode, finds the old entry,
 * adds with the new name, and updates the hash.
 *
 * Original address: 0x00E57562
 * Original size: 392 bytes
 */

#include "dir/dir_internal.h"

/*
 * DIR_$OLD_CNAMEU - Legacy change name (rename) entry
 *
 * The process is:
 * 1. Validate both old and new leaf names via FUN_00e54414
 * 2. Enter super mode / acquire directory lock via FUN_00e54854
 * 3. Find the old entry by name via FUN_00e54b9e
 * 4. Add the entry with the new name:
 *    - Root directory: FUN_00e55406
 *    - Non-root: FUN_00e55220
 * 5. Update the hash table via FUN_00e555dc
 * 6. Release lock via FUN_00e54734
 * 7. Exit super mode via ACL_$EXIT_SUPER
 *
 * Parameters:
 *   dir_uid      - UID of directory containing entry
 *   old_name     - Current name
 *   old_name_len - Pointer to current name length
 *   new_name     - New name
 *   new_name_len - Pointer to new name length
 *   status_ret   - Output: status code
 */
void DIR_$OLD_CNAMEU(uid_t *dir_uid, char *old_name, uint16_t *old_name_len,
                     char *new_name, uint16_t *new_name_len, status_$t *status_ret)
{
    uint8_t old_parsed[256];
    uint16_t old_parsed_len;
    uint8_t new_parsed[256];
    uint16_t new_parsed_len;
    uint32_t handle;
    int32_t entry;
    uint16_t param5, param6;
    int8_t valid;
    int8_t found;
    uint16_t hash;
    uint8_t result_buf[8];
    uint8_t entry_type;

    /* Validate old leaf name */
    valid = FUN_00e54414(old_name, *old_name_len, old_parsed, &old_parsed_len);
    if (valid >= 0) {
        *status_ret = status_$naming_invalid_leaf;
        return;
    }

    /* Validate new leaf name */
    valid = FUN_00e54414(new_name, *new_name_len, new_parsed, &new_parsed_len);
    if (valid >= 0) {
        *status_ret = status_$naming_invalid_leaf;
        return;
    }

    /* Enter super mode / acquire directory lock */
    FUN_00e54854(dir_uid, &handle, 0x40002, status_ret);
    if ((int16_t)*status_ret != 0) {
        ACL_$EXIT_SUPER();
        return;
    }

    /* Find the old entry by name */
    found = FUN_00e54b9e(handle, old_parsed, old_parsed_len,
                         &entry, &param5, &param6);
    if (found >= 0) {
        *status_ret = status_$naming_name_not_found;
        FUN_00e54734(status_ret);
        ACL_$EXIT_SUPER();
        return;
    }

    /* Read the entry type */
    entry_type = *((uint8_t *)(entry + 0x27));

    /* Add entry with new name - dispatch based on root vs non-root */
    if (dir_uid->high == NAME_$ROOT_UID.high &&
        dir_uid->low == NAME_$ROOT_UID.low) {
        /* Root directory path */
        FUN_00e55406(dir_uid, handle, new_parsed, new_parsed_len,
                     entry_type, (void *)(entry + 0x28),
                     0, 0xFF, result_buf, status_ret);
    } else {
        /* Non-root directory path */
        FUN_00e55220(dir_uid, handle, new_parsed, new_parsed_len,
                     entry_type, (void *)(entry + 0x28),
                     0, result_buf, status_ret);
    }

    if ((int16_t)*status_ret == 0) {
        /* Compute new hash and update */
        hash = FUN_00e54b58(new_parsed, new_parsed_len, 0);
        FUN_00e555dc(handle, param5, param6, hash);
    }

    /* Release directory lock */
    FUN_00e54734(status_ret);

    ACL_$EXIT_SUPER();
}
