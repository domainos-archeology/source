/*
 * NAME_$VALIDATE - Validate a pathname and determine its type
 *
 * Validates the pathname length and determines the path type:
 * - Relative paths (no leading /)
 * - Absolute paths (single leading /)
 * - Network paths (double leading //)
 * - Node data paths (leading `node_data)
 *
 * Original address: 0x00e49f4c
 * Original size: 158 bytes
 */

#include "name/name_internal.h"

/* String constants for node_data path matching */
static const char s_node_data[] = "`node_data";      /* 10 chars */
static const char s_node_data_slash[] = "`node_data/"; /* 11 chars */
static const uint16_t node_data_len = 10;
static const uint16_t node_data_slash_len = 11;

/*
 * NAME_$VALIDATE - Validate pathname and determine type
 *
 * Parameters:
 *   path            - The pathname to validate
 *   path_len        - Pointer to pathname length (Pascal string style)
 *   consumed        - Output: number of leading characters consumed
 *   start_path_type - Output: the determined path type
 *
 * Returns:
 *   0xFF (always returns true, sets start_path_type to error if invalid)
 *
 * Note: The original code returns 0xFF in all cases. Error conditions
 * are signaled via start_path_type = start_path_$error.
 */
boolean NAME_$VALIDATE(char *path, uint16_t *path_len, int16_t *consumed,
                       start_path_type_t *start_path_type)
{
    uint16_t len = *path_len;

    /* Check for path too long (max 256 characters) */
    if (len > NAME_$MAX_PNAME_LEN) {
        *start_path_type = start_path_$error;
        return true;
    }

    /* Default to relative path */
    *start_path_type = start_path_$relative;

    /* Empty path is valid relative path */
    if (len == 0) {
        return true;
    }

    /* Check for leading '/' (absolute or network path) */
    if (path[0] == '/') {
        *start_path_type = start_path_$absolute;
        (*consumed)++;

        /* Check for '//' (network path) */
        if (len >= 2 && path[1] == '/') {
            *start_path_type = start_path_$network;
            (*consumed)++;
        }
        return true;
    }

    /* Check for node_data path (starts with backtick) */
    if (path[0] == '`') {
        /* Exact match for "`node_data" (10 chars) */
        if (len == 10) {
            if (NAMEQ(path, path_len, (char *)s_node_data, (uint16_t *)&node_data_len)) {
                *start_path_type = start_path_$node_data;
            }
        }
        /* Match for "`node_data/..." (11+ chars) */
        else if (len >= 11) {
            if (NAMEQ(path, (uint16_t *)&node_data_slash_len,
                      (char *)s_node_data_slash, (uint16_t *)&node_data_slash_len)) {
                *start_path_type = start_path_$node_data;
            }
        }
        /* Paths starting with ` but not matching `node_data are invalid */
        /* Note: original returns 0xff even in this case */
    }

    return true;
}
