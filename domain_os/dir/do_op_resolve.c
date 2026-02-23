/*
 * dir_$do_op_resolve - DO_OP handler for pathname resolution
 *
 * Local handler for the RESOLVE operation (opcode 0x58). Walks a pathname
 * component by component, resolving each leaf name via dir_$do_op_get_entryu.
 * Handles special cases:
 *   - '/' separators (skipped)
 *   - '.' (current directory, skipped)
 *   - '..' (parent directory, resolved via dir_$get_parent_uid)
 *   - Symbolic links (type 3): reads link target via dir_$do_op_read_linku
 *   - Normal entries (type 2/3): descends into the resolved UID
 *
 * Includes timeout protection for non-server processes: if resolution
 * takes more than 0x15 clock ticks after the first two components,
 * sets the "loop" flag and returns.
 *
 * Also detects and breaks cross-node root cycles: if after the second
 * component the parent UID points to NAME_$ROOT_UID on a different node,
 * sets the loop flag.
 *
 * Parameters:
 *   path_data   - Pointer to 1-based pathname string
 *   path_len    - Length of pathname
 *   result      - Resolve result structure (copied from request, updated)
 *   uid_ret     - Output: resolved object UID
 *   extra       - Output: extra attribute data
 *   flags1      - Output: 0xFF initially (success flag)
 *   flags2      - Output: loop detection flag (0xFF if loop)
 *   cont        - In/out: continuation position (current offset in path)
 *   size        - In/out: current component length
 *   eof         - Output: last resolved component start
 *   count       - Output: last resolved component length
 *   max         - Max response version (passed to sub-calls)
 *   link_count  - Output: link nesting count
 *   status_ret  - Output: status code
 *
 * Original address: 0x00E4D0E2
 * Original size: 628 bytes
 */

#include "dir/dir_internal.h"

/* TIME_$CLOCKH is declared in time/time.h (included via dir_internal.h -> base.h chain)
 * as uint32_t. The dir_internal.h includes time/time.h indirectly. */
#include "time/time.h"

/* dir_$get_parent_uid - Resolve parent UID of a directory */
void dir_$get_parent_uid(uid_t *uid, status_$t *status_ret);

/* DIR_$IS_RETRYABLE_STATUS - declared in dir_internal.h with status_$t parameter */

void dir_$do_op_resolve(uint32_t path_data, uint16_t path_len, void *result,
                        uint32_t *extra_ret, uint32_t *parent_uid_ret,
                        uint8_t *flags1, uint8_t *flags2,
                        uint16_t *cont, uint16_t *size,
                        uint16_t *last_start, uint16_t *last_size,
                        uint32_t max, uint16_t *link_count,
                        status_$t *status_ret)
{
    uid_t *dir_uid = (uid_t *)result;
    char *path = (char *)path_data;  /* 1-based pathname */
    uint16_t pos;
    uint16_t comp_len;
    uint16_t depth;
    int8_t is_server_proc;
    uint32_t start_time;
    int16_t entry_type;
    uid_t entry_uid;
    uint32_t entry_extra;

    /* Initialize outputs */
    *flags1 = 0xFF;
    *flags2 = 0;
    *link_count = 0;
    *status_ret = status_$ok;
    depth = 0;
    *extra_ret = 0;

    /* Record start time for timeout detection */
    start_time = TIME_$CLOCKH;

    /* Check if current process is a server (type 9) */
    is_server_proc = (((uint16_t *)PROC1_$TYPE)[(int16_t)(PROC1_$CURRENT)] == 9)
                     ? (int8_t)-1 : 0;

    /* Main resolution loop */
    while (*cont <= path_len) {
        pos = *cont;

        /* Skip slash separators */
        if (path[pos - 1] == '/') {
            *cont = *cont + 1;
            continue;
        }

        /* Find end of current component */
        comp_len = pos;
        while (comp_len <= path_len && path[comp_len - 1] != '/') {
            comp_len++;
        }
        comp_len = comp_len - *cont;
        *size = comp_len;

        /* Check leaf name length limit */
        if (comp_len > 0xFF) {
            *status_ret = status_$naming_invalid_leaf;
            goto check_exit;
        }

        /* Check for '.' (current directory) */
        if (comp_len == 1 && path[*cont - 1] == '.') {
            *last_start = *cont;
            *last_size = *size;
            *cont = *cont + 1;
            continue;
        }

        /* This is a real component */
        depth++;

        /* Timeout check for non-server processes after 2nd component */
        if (is_server_proc >= 0 && depth >= 2) {
            if (TIME_$CLOCKH - start_time > 0x14) {
                *flags2 = 0xFF;
                goto check_exit;
            }
        }

        /* Check for '..' (parent directory) */
        if (*size == 2 && path[*cont - 1] == '.' && path[*cont] == '.') {
            /* Resolve parent */
            if (dir_uid->high != NAME_$ROOT_UID.high ||
                dir_uid->low != NAME_$ROOT_UID.low) {
                if (dir_uid->high == NAME_$NODE_UID.high &&
                    dir_uid->low == NAME_$NODE_UID.low) {
                    /* //node_data -> root */
                    dir_uid->high = NAME_$ROOT_UID.high;
                    dir_uid->low = NAME_$ROOT_UID.low;
                } else {
                    dir_$get_parent_uid(dir_uid, status_ret);
                    if (*status_ret != status_$ok) goto check_exit;
                }
            }
            /* At root, '..' is a no-op */
            *last_start = *cont;
            *last_size = *size;
            *cont = *cont + 2;
            /* Clear parent tracking UID */
            parent_uid_ret[0] = UID_$NIL.high;
            parent_uid_ret[1] = UID_$NIL.low;
            continue;
        }

        /* Cross-node root cycle detection */
        if (depth == 2) {
            uint32_t *parent = parent_uid_ret;
            if ((parent[1] & 0xFFFFF) != (dir_uid->low & 0xFFFFF)) {
                if (parent[0] == NAME_$ROOT_UID.high &&
                    parent[1] == NAME_$ROOT_UID.low) {
                    /* Different node's root - loop detected */
                    *flags2 = 0xFF;
                    goto check_exit;
                }
            }
        }

        /* Look up the component in the current directory */
        dir_$do_op_get_entryu(dir_uid, path + *cont - 1, *size,
                              &entry_type, (char *)&entry_uid, &entry_extra,
                              status_ret);

        {
            status_$t s = *status_ret;
            /* Record last resolved component on success or name_not_found */
            if (s == status_$ok || s == status_$naming_name_not_found) {
                *last_start = *cont;
                *last_size = *size;
            }
            if (s != status_$ok) goto check_exit;
        }

        /* Check if this is a symbolic link */
        if (entry_type == 3) {
            /* Read the link target */
            dir_$do_op_read_linku(dir_uid, path + *cont - 1, *size,
                                  0x3FF, max,
                                  link_count, &entry_uid,
                                  status_ret);
            goto check_exit;
        }

        /* Normal entry - descend */
        /* Save current dir_uid as parent */
        parent_uid_ret[0] = dir_uid->high;
        parent_uid_ret[1] = dir_uid->low;

        /* Move to resolved entry */
        dir_uid->high = entry_uid.high;
        dir_uid->low = entry_uid.low;
        *extra_ret = entry_extra;

        /* Advance past this component */
        *cont = *cont + *size;
    }

check_exit:
    /* On retryable error (not from first component), clear status and set loop flag */
    if (*status_ret != status_$ok) {
        int8_t retryable = DIR_$IS_RETRYABLE_STATUS(*status_ret);
        if (retryable < 0 && depth != 1) {
            *status_ret = status_$ok;
            *flags2 = 0xFF;
        }
    }
}
