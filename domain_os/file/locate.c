/*
 * FILE_$LOCATE - Get file location from UID
 *
 * Original address: 0x00E60620
 * Size: 92 bytes
 *
 * This function retrieves the location (node) information for a file
 * given its UID. It clears a "local" flag in the UID before calling
 * AST_$GET_LOCATION.
 *
 * Assembly analysis:
 *   - link.w A6,-0x34       ; Stack frame with local variables
 *   - Copies input UID to local storage
 *   - Clears bit 6 of the flags byte (makes it remote-capable)
 *   - Calls AST_$GET_LOCATION at 0x00E046C8
 *   - Returns location info in param_2
 */

#include "file/file_internal.h"

/*
 * FILE_$LOCATE - Get file location from UID
 *
 * Retrieves the location (network node) information for a file object.
 * Clears the "local only" flag before querying.
 *
 * Parameters:
 *   file_uid     - Pointer to file UID to locate
 *   location_out - Output: receives location info (uint32_t node address)
 *   status_ret   - Output: status code
 *
 * Note: The input UID structure appears to have:
 *   - 8 bytes for the UID proper
 *   - Additional bytes including a flags field at offset 5 (byte position)
 *     where bit 6 indicates "local only"
 */
void FILE_$LOCATE(uid_t *file_uid, uint32_t *location_out, status_$t *status_ret)
{
    status_$t status;
    uid_t local_uid;
    uint8_t *flags_ptr;

    /* Local buffers for AST_$GET_LOCATION outputs */
    uint32_t ast_output1;
    uint32_t ast_output2;

    /* Extended UID structure for the location query */
    struct {
        uid_t uid;
        uint32_t location;
        uint8_t padding[4];
    } query_buf;

    /* Copy input UID to local buffer */
    local_uid.high = file_uid->high;
    local_uid.low = file_uid->low;

    /* Copy to query buffer */
    query_buf.uid.high = local_uid.high;
    query_buf.uid.low = local_uid.low;

    /*
     * Clear bit 6 of the flags byte in the UID
     * This removes the "local only" constraint, allowing the query
     * to return remote location information.
     *
     * The flags byte is at offset 5 of the extended query structure
     * (which maps to byte 5 of the 8-byte UID portion)
     */
    flags_ptr = (uint8_t *)&query_buf.uid + 5;
    *flags_ptr &= ~0x40;  /* Clear bit 6 */

    /* Call AST_$GET_LOCATION to get the file's location */
    AST_$GET_LOCATION(&query_buf, 0, &ast_output1, &ast_output2, &status);

    /* Return the location info */
    *location_out = query_buf.location;
    *status_ret = status;
}
