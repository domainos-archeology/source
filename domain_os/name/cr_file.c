/*
 * NAME_$CR_FILE - Create a new file with a given name
 *
 * Creates a new file in the filesystem and adds it to the specified
 * directory with the given name.
 *
 * Original address: 0x00e4a316
 * Original size: 186 bytes
 */

#include "name/name_internal.h"

/*
 * NAME_$CR_FILE - Create a file with the given pathname
 *
 * Creates a new file by:
 * 1. Resolving the parent directory from the pathname
 * 2. Creating an empty file object
 * 3. Copying ACL from parent directory to the new file
 * 4. Adding the file entry to the parent directory
 *
 * If any step fails after file creation, the file is deleted and
 * the returned UID is set to NIL.
 *
 * Parameters:
 *   path       - Pathname for the new file
 *   path_len   - Pointer to pathname length
 *   file_ret   - Output: UID of the created file (or NIL on failure)
 *   status_ret - Output: status code
 */
void NAME_$CR_FILE(char *path, int16_t *path_len, uid_t *file_ret, status_$t *status_ret)
{
    uint16_t filename_idx;
    int16_t filename_len;
    uid_t parent_dir_uid;
    status_$t status;

    /* Resolve the parent directory and get filename position */
    if (!name_$resolve_dir_and_leaf(path, *path_len, &filename_idx, &filename_len,
                                    &parent_dir_uid, status_ret)) {
        return;
    }

    /* Create the file object */
    FILE_$CREATE(&parent_dir_uid, file_ret, status_ret);

    /* Check status - original code only checks low word */
    if ((int16_t)*status_ret != 0) {
        return;
    }

    /* Copy ACL from parent directory to new file */
    ACL_$COPY(&parent_dir_uid, file_ret, &ACL_$FILE_ACL, &ACL_$FILEIN_ACL, status_ret);

    if (*status_ret != status_$ok) {
        goto cleanup;
    }

    /* Add the file entry to the directory */
    DIR_$ADDU(&parent_dir_uid, path + (filename_idx - 1), &filename_len, file_ret, status_ret);

    if (*status_ret == status_$ok) {
        /* Success - return with file UID */
        return;
    }

cleanup:
    /* Failed - delete the file and return NIL */
    FILE_$DELETE(file_ret, &status);
    file_ret->high = UID_$NIL.high;
    file_ret->low = UID_$NIL.low;
}
