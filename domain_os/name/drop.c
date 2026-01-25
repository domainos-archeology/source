/*
 * NAME_$DROP - Drop/delete a named object
 *
 * Removes a named entry from its parent directory.
 *
 * Original address: 0x00e4a2b8
 * Original size: 94 bytes
 */

#include "name/name_internal.h"

/*
 * name_$split_path - Split path into directory and filename portions
 *
 * Scans backwards from end of path to find the last '/'.
 * Returns the directory length (without trailing slash, except for root)
 * and the filename position and length.
 *
 * Parameters:
 *   path             - The full pathname
 *   path_len         - Length of pathname (1-indexed)
 *   dirname_len_ret  - Output: length of directory portion
 *   filename_idx_ret - Output: 1-indexed position of filename start
 *   filename_len_ret - Output: length of filename
 *
 * Original address: 0x00e49e48
 */
void name_$split_path(char *path, uint16_t path_len, uint16_t *dirname_len_ret,
                      uint16_t *filename_idx_ret, int16_t *filename_len_ret)
{
    uint16_t pos = path_len;

    *filename_len_ret = 0;

    /* Scan backwards from end to find last '/' */
    while (pos <= path_len && path[pos - 1] != '/') {
        (*filename_len_ret)++;
        *filename_idx_ret = pos;
        pos--;
    }

    *dirname_len_ret = pos;

    /* Adjust dirname_len to exclude trailing slash, except for:
     * - Root "/" (pos=1)
     * - Network root "//" (pos=2, first two chars are '/')
     */
    if (*dirname_len_ret > 1) {
        /* Don't strip if it's "//" at start */
        if (!(path[1] == '/' && *dirname_len_ret == 2 && path[0] == '/')) {
            (*dirname_len_ret)--;
        }
    }
}

/*
 * name_$resolve_dir_and_leaf - Resolve directory and get leaf info
 *
 * Splits a path into directory and filename, then resolves the directory
 * to its UID.
 *
 * Parameters:
 *   path             - The full pathname
 *   path_len         - Length of pathname
 *   filename_idx_ret - Output: 1-indexed position of filename
 *   filename_len_ret - Output: length of filename
 *   dir_uid_ret      - Output: UID of the parent directory
 *   status_ret       - Output: status code
 *
 * Returns:
 *   true (0xFF) if directory was found, false (0) otherwise
 *
 * Original address: 0x00e4a1e2
 */
boolean name_$resolve_dir_and_leaf(char *path, int16_t path_len,
                                   uint16_t *filename_idx_ret, int16_t *filename_len_ret,
                                   uid_t *dir_uid_ret, status_$t *status_ret)
{
    boolean found = false;
    uint16_t dirname_len;
    uid_t parent_uid;

    /* Split path into directory and filename */
    name_$split_path(path, path_len, &dirname_len, filename_idx_ret, filename_len_ret);

    /* If no filename, path is invalid */
    if (*filename_len_ret == 0) {
        *status_ret = status_$naming_invalid_pathname;
        return found;
    }

    /* Resolve the directory portion */
    name_$resolve_internal(path, dirname_len, &parent_uid, dir_uid_ret, status_ret);

    /* Convert "directory not found in pathname" to "name not found" */
    if (*status_ret == status_$naming_directory_not_found_in_pathname) {
        *status_ret = status_$naming_name_not_found;
    }

    if (*status_ret == status_$ok) {
        found = true;
    }

    return found;
}

/*
 * NAME_$DROP - Drop/delete a named object
 *
 * Parameters:
 *   path       - Pathname of object to drop
 *   path_len   - Pointer to pathname length
 *   file_uid   - UID of the file (for verification)
 *   status_ret - Output: status code
 */
void NAME_$DROP(char *path, int16_t *path_len, uid_t *file_uid, status_$t *status_ret)
{
    uint16_t filename_idx;
    int16_t filename_len;
    uid_t dir_uid;

    /* Resolve directory and get filename position */
    if (name_$resolve_dir_and_leaf(path, *path_len, &filename_idx, &filename_len,
                                   &dir_uid, status_ret)) {
        /* Drop the entry from the directory */
        DIR_$DROPU(&dir_uid, path + (filename_idx - 1), &filename_len, file_uid, status_ret);
    }
}
