/*
 * ASKNODE_$GET_INFO - Get node information with explicit params
 *
 * Wrapper for ASKNODE_$INTERNET_INFO that passes through all parameters,
 * but still uses a default request length.
 *
 * Original address: 0x00E645C4
 * Size: 38 bytes
 *
 * Assembly analysis:
 *   - Similar structure to ASKNODE_$INFO
 *   - Uses PC-relative addressing for constant data at 0xE645C0
 *   - Passes param2 as the response length (unlike INFO which uses default)
 *   - Calls ASKNODE_$INTERNET_INFO with 7 parameters
 */

#include "asknode/asknode_internal.h"

/*
 * Default request length (from PC-relative data at 0xE645C0)
 */
static int32_t default_req_len;

void ASKNODE_$GET_INFO(uint16_t *req_type, uint32_t *node_id,
                       uid_t *param1, uint16_t *param2,
                       uint32_t *result, status_$t *status)
{
    /*
     * Call INTERNET_INFO with default request length but caller-provided
     * response length.
     */
    ASKNODE_$INTERNET_INFO(req_type, node_id, &default_req_len,
                           param1, param2, result, status);
}
