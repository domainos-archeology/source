/*
 * ASKNODE_$INFO - Get node information (local node only)
 *
 * Simplified wrapper for ASKNODE_$INTERNET_INFO that queries the local node.
 * Passes default request length and response length parameters.
 *
 * Original address: 0x00E64598
 * Size: 38 bytes
 *
 * Assembly analysis:
 *   - Builds stack frame, then pushes parameters onto stack
 *   - Uses PC-relative addressing for constant data at 0xE645BE and 0xE645C0
 *   - DAT_00e645be = default response length parameter
 *   - DAT_00e645c0 = default request length parameter
 *   - Calls ASKNODE_$INTERNET_INFO and returns
 */

#include "asknode/asknode_internal.h"

/*
 * Default parameter values (from PC-relative data)
 * These are located immediately after the function in the binary.
 */
static int32_t default_req_len;      /* At 0xE645C0 - request length */
static uint16_t default_resp_len;    /* At 0xE645BE - response length */

void ASKNODE_$INFO(uint16_t *req_type, uint32_t *node_id,
                   uid_t *param, uint32_t *result, status_$t *status)
{
    /*
     * Call INTERNET_INFO with default length parameters.
     * The default values would be constants in the original binary
     * (PC-relative data at 0xE645BE and 0xE645C0).
     */
    ASKNODE_$INTERNET_INFO(req_type, node_id, &default_req_len,
                           param, &default_resp_len, result, status);
}
