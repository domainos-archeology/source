/*
 * FILE_$LOCATEI - Get file location with diskless fallback
 *
 * Original address: 0x00E6067C
 * Size: 182 bytes
 *
 * This function retrieves the location (node) information for a file.
 * If AST_$GET_LOCATION fails, it checks if the UID represents a
 * diskless client file and handles the fallback case by computing
 * the location from the diskless UID structure.
 *
 * Assembly analysis:
 *   - link.w A6,-0x38       ; Stack frame with local variables
 *   - Similar setup to FILE_$LOCATE
 *   - On failure, checks if first byte is 0 and second byte matches DISKLESS_$UID
 *   - If diskless, extracts node info from low 20 bits and calls DIR_$FIND_NET
 */

#include "file/file_internal.h"
#include "dir/dir.h"

/*
 * External reference to diskless UID pattern
 * Located at 0x00E173F4 (DISKLESS_$UID)
 * The second byte (offset 1) contains the diskless node identifier
 */
extern uid_t DISKLESS_$UID;

/*
 * DIR_$FIND_NET - Find network node for a directory entry
 *
 * Original address: 0x00E4E8B4
 *
 * Parameters:
 *   param_1 - Unknown (0x29C = 668, possibly a table offset)
 *   index   - Index value (low 20 bits of UID)
 *
 * Returns:
 *   High part of node address
 */
extern uint32_t DIR_$FIND_NET(uint32_t param_1, uint32_t *index);

/*
 * FILE_$LOCATEI - Get file location with diskless fallback
 *
 * Extended version of FILE_$LOCATE that handles diskless client UIDs.
 * If the normal location lookup fails and the UID appears to be a
 * diskless client UID, computes the location from the UID structure.
 *
 * Parameters:
 *   file_uid     - Pointer to file UID to locate
 *   location_out - Output: receives location UID (high + low)
 *   status_ret   - Output: status code
 *
 * Diskless UID format:
 *   - Byte 0: Must be 0
 *   - Byte 1: Must match DISKLESS_$UID.high byte 1
 *   - Low 20 bits of uid.low: Index for DIR_$FIND_NET
 */
void FILE_$LOCATEI(uid_t *file_uid, uid_t *location_out, status_$t *status_ret)
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
        uid_t location;
    } query_buf;

    /* Copy input UID to local buffer */
    local_uid.high = file_uid->high;
    local_uid.low = file_uid->low;

    /* Copy to query buffer */
    query_buf.uid.high = local_uid.high;
    query_buf.uid.low = local_uid.low;

    /*
     * Clear bit 6 of the flags byte in the UID
     * This removes the "local only" constraint
     */
    flags_ptr = (uint8_t *)&query_buf.uid + 5;
    *flags_ptr &= ~0x40;  /* Clear bit 6 */

    /* Call AST_$GET_LOCATION to get the file's location */
    AST_$GET_LOCATION(&query_buf, 0, &ast_output1, &ast_output2, &status);

    if (status == status_$ok) {
        /* Success - return the location from the query buffer */
        location_out->high = query_buf.location.high;
        location_out->low = query_buf.location.low;
    } else {
        /*
         * Location lookup failed - check for diskless client UID
         *
         * Diskless UIDs have:
         *   - First byte (high >> 24) = 0
         *   - Second byte ((high >> 16) & 0xFF) = DISKLESS_$UID identifier
         */
        uint8_t byte0 = (uint8_t)(local_uid.high >> 24);
        uint8_t byte1 = (uint8_t)(local_uid.high >> 16);
        uint8_t diskless_id = (uint8_t)(DISKLESS_$UID.high >> 16);

        if (byte0 == 0 && byte1 == diskless_id) {
            /*
             * This is a diskless client UID
             * Extract index from low 20 bits and find network node
             */
            uint32_t index = local_uid.low & 0xFFFFF;

            /* Store index in low part */
            location_out->low = index;

            /* Find network node for this diskless entry */
            location_out->high = DIR_$FIND_NET(0x29C, &index);

            /* Override status to success */
            status = status_$ok;
        }
    }

    *status_ret = status;
}
