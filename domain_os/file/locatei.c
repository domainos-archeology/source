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
#include "name/name.h"      /* NAME_$ROOT_UID */

/* DISKLESS_$UID (0x00E173F4) is declared in uid/uid.h */

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

    /* Volume UID output from AST_$GET_LOCATION */
    uint32_t vol_uid_out;

    /*
     * Extended UID structure for the location query
     * AST_$GET_LOCATION expects UID at offset 8, and writes
     * 32 bytes of location info back to the buffer
     */
    file_$obj_loc_t query_buf;      /* A6-0x20 */
    uint32_t loc_unused;            /* A6-0x34: pea'd, never touched */

    /* Copy input UID to local buffer */
    local_uid.high = file_uid->high;
    local_uid.low = file_uid->low;

    /* Copy to query buffer at offset 8 */
    query_buf.uid.high = local_uid.high;
    query_buf.uid.low = local_uid.low;

    /*
     * Clear bit 6 of the record's flags byte at +0x1D
     * (0x00E606A6 `bclr.b #0x6,(-0x3,A6)` with the record based at A6-0x20).
     * This removes the "local only" constraint.
     */
    query_buf.flags &= (int8_t)~FILE_OBJ_LOC_SCRATCH;

    /* Call AST_$GET_LOCATION to get the file's location */
    AST_$GET_LOCATION(&query_buf, 0, &loc_unused, &vol_uid_out, &status);

    if (status == status_$ok) {
        /* 0x00E606D0: the two longwords at record+0x10 and record+0x14 */
        location_out->high = query_buf.loc_info;
        location_out->low = query_buf.node;
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
            /* 0x00E6070E pushes the longword 0x00E8029C = &NAME_$ROOT_UID. */
            location_out->high = DIR_$FIND_NET(&NAME_$ROOT_UID, &index);

            /* Override status to success */
            status = status_$ok;
        }
    }

    *status_ret = status;
}
