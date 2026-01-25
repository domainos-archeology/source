/*
 * NAME_$RESOLVE - Resolve a pathname to a UID
 *
 * Converts a pathname string to the UID of the named object.
 * Uses internal helper functions to parse path components and
 * traverse the directory hierarchy.
 *
 * Original address: 0x00e4a258
 * Original size: 96 bytes
 */

#include "name/name_internal.h"

/*
 * name_$parse_component - Parse next path component
 *
 * Parses the next component from a pathname, skipping leading slashes.
 *
 * Parameters:
 *   path         - The full pathname
 *   path_len     - Total length of pathname (1-indexed)
 *   start_pos    - Current position in path (1-indexed)
 *   next_pos     - Output: position after this component (1-indexed)
 *   comp_start   - Output: start of component (1-indexed)
 *   comp_len     - Output: length of component
 *
 * Original address: 0x00e4a004
 */
static void name_$parse_component(char *path, uint16_t path_len, uint16_t start_pos,
                                  uint16_t *next_pos, uint16_t *comp_start, int16_t *comp_len)
{
    uint16_t pos = start_pos;

    *comp_len = 0;
    *next_pos = pos;

    /* Skip leading slashes */
    while (pos <= path_len && path[pos - 1] == '/') {
        pos++;
        *next_pos = pos;
    }

    /* Check if we've reached the end */
    if (pos > path_len) {
        return;
    }

    /* Mark start of component */
    *comp_start = pos;

    /* Count characters until slash or end */
    while (pos <= path_len && path[pos - 1] != '/') {
        pos++;
        (*comp_len)++;
        *next_pos = pos;
    }
}

/*
 * name_$resolve_internal - Internal pathname resolution
 *
 * Called by NAME_$RESOLVE to perform the actual resolution.
 * Handles different path types and traverses directory entries.
 *
 * Parameters:
 *   path         - The pathname to resolve
 *   path_len     - Length of pathname (value, not pointer)
 *   dir_uid_ret  - Output: UID of containing directory
 *   file_uid_ret - Output: UID of the named object
 *   status_ret   - Output: status code
 *
 * Original address: 0x00e4a060
 */
void name_$resolve_internal(char *path, int16_t path_len, uid_t *dir_uid_ret,
                            uid_t *file_uid_ret, status_$t *status_ret)
{
    uid_t current_uid;
    uint16_t word_path_len = (uint16_t)path_len;
    int16_t consumed = 0;
    start_path_type_t start_path_type;
    uint16_t next_pos;
    uint16_t comp_start;
    int16_t comp_len;
    int16_t entry_type;

    /* Initialize outputs to NIL */
    dir_uid_ret->high = UID_$NIL.high;
    dir_uid_ret->low = UID_$NIL.low;
    file_uid_ret->high = UID_$NIL.high;
    file_uid_ret->low = UID_$NIL.low;

    /* Validate and determine path type */
    NAME_$VALIDATE(path, &word_path_len, &consumed, &start_path_type);

    /* Initialize current UID based on path type */
    switch (start_path_type) {
        case start_path_$absolute:
            consumed = 2;  /* Skip the leading '/' */
            NAME_$GET_NODE_UID(&current_uid);
            break;

        case start_path_$node_data:
            NAME_$GET_NODE_DATA_UID(&current_uid);
            consumed = 11;  /* Skip "`node_data" */
            if (path_len > 10 && path[10] == '/') {
                consumed = 12;  /* Skip "`node_data/" */
            }
            break;

        case start_path_$relative:
            /* For relative paths, we'd start from working directory */
            /* Fall through to default processing */
            break;

        case start_path_$network:
        case start_path_$error:
            *status_ret = status_$naming_invalid_pathname;
            return;

        default:
            break;
    }

    /* Parse and resolve path components */
    for (;;) {
        name_$parse_component(path, word_path_len, consumed, &next_pos, &comp_start, &comp_len);
        consumed = next_pos;

        /* End of path - return current as file */
        if (comp_len == 0) {
            file_uid_ret->high = current_uid.high;
            file_uid_ret->low = current_uid.low;
            *status_ret = status_$ok;
            return;
        }

        /* Handle "." component - skip it */
        if (comp_len == 1 && path[comp_start - 1] == '.') {
            continue;
        }

        /* Copy current to dir before looking up next */
        dir_uid_ret->high = current_uid.high;
        dir_uid_ret->low = current_uid.low;

        /* Handle ".." component - invalid for now */
        if (comp_len == 2 && path[comp_start - 1] == '.' && path[comp_start] == '.') {
            *status_ret = status_$naming_invalid_pathname;
            return;
        }

        /* Look up the component in the directory */
        DIR_$GET_ENTRYU(dir_uid_ret, path + comp_start - 1, (uint16_t *)&comp_len,
                        &entry_type, status_ret);

        if (*status_ret != status_$ok) {
            return;
        }

        /* Check entry type from directory lookup */
        if (entry_type == 0) {
            /* Entry not found */
            *status_ret = status_$naming_name_not_found;
            return;
        }
        else if (entry_type == 1) {
            /* Entry is a directory - continue traversal */
            /* The DIR_$GET_ENTRYU should have returned the UID */
            /* For now, continue with the same current_uid */
            continue;
        }
        else if (entry_type == 3) {
            /* Entry type 3 - invalid for path traversal */
            *status_ret = status_$naming_invalid_pathname;
            return;
        }

        /* Other entry types - continue traversal */
    }
}

/*
 * NAME_$RESOLVE - Resolve a pathname to a UID
 *
 * Parameters:
 *   path         - The pathname to resolve
 *   path_len     - Pointer to pathname length
 *   resolved_uid - Output: UID of the resolved object
 *   status_ret   - Output: status code
 */
void NAME_$RESOLVE(char *path, int16_t *path_len, uid_t *resolved_uid, status_$t *status_ret)
{
    uid_t dir_uid;
    uid_t file_uid;

    /* Initialize output to NIL */
    resolved_uid->high = UID_$NIL.high;
    resolved_uid->low = UID_$NIL.low;

    /* Call internal resolver */
    name_$resolve_internal(path, *path_len, &dir_uid, &file_uid, status_ret);

    /* Convert "directory not found in pathname" to "name not found" */
    if (*status_ret == status_$naming_directory_not_found_in_pathname) {
        *status_ret = status_$naming_name_not_found;
    }

    /* On success, copy the file UID to output */
    if (*status_ret == status_$ok) {
        resolved_uid->high = file_uid.high;
        resolved_uid->low = file_uid.low;
    }
}
