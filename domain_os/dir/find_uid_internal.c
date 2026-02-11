/*
 * dir_$find_uid_internal - Internal find UID helper
 *
 * Shared implementation for DIR_$FIND_UID (flag=0) and
 * DIR_$FIND_NET (flag=0xFF). Sends a DO_OP request with command
 * byte 0x46 ('F') and opcode 0x11A to try the new protocol first.
 *
 * Process:
 * 1. Construct request buffer with cmd=0x46, two UIDs, and flag byte
 * 2. Call DIR_$DO_OP with opcode 0x11A
 * 3. On error (file_$bad_reply or status_$naming_bad_directory):
 *    - flag < 0: call DIR_$OLD_FIND_NET (masked to 20-bit UID)
 *    - flag >= 0: call DIR_$OLD_FIND_UID to get name
 * 4. On success:
 *    - flag < 0: return 4-byte network value in *net_ret
 *    - flag >= 0: copy name from response, truncate if needed
 *      (returns status_$naming_leaf_truncated if buffer too small)
 *
 * Parameters:
 *   dir_uid      - UID of directory to search
 *   target_uid   - UID to find
 *   flag         - Negative for network search, non-negative for UID search
 *   name_buf_len - Max buffer length for name output
 *   name_buf     - Output: name buffer
 *   name_len_ret - Output: actual name length
 *   net_ret      - Output: network address (network mode only)
 *   status_ret   - Output: status code
 *
 * Original address: 0x00E4E786
 * Size: 246 bytes
 *
 * TODO: Full implementation requires DIR_$DO_OP request/response
 * buffer format for opcode 0x11A.
 */

#include "dir/dir_internal.h"

/* Stub - 246-byte find UID with DO_OP + old fallback */
