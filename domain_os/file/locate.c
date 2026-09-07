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
 * Note: AST_$GET_LOCATION takes a 0x20-byte location record, not a bare
 * UID: the UID sits at +0x08 and the flags byte (bit 6 = "local only") at
 * +0x1D.
 */
void FILE_$LOCATE(uid_t *file_uid, uint32_t *location_out, status_$t *status_ret)
{
    status_$t status;

    /* Volume UID output from AST_$GET_LOCATION */
    uint32_t vol_uid_out;

    /*
     * A6-0x20: the 0x20-byte AST location record.  The UID goes in at
     * +0x08 (0x00E6063C) and bit 6 of the flags byte at +0x1D is cleared
     * (0x00E60644 `bclr.b #6,(-0x3,A6)`); the routine then overwrites all
     * 0x20 bytes.
     */
    file_$obj_loc_t loc_rec;
    uid_t local_uid;               /* A6-0x28 */
    uint32_t loc_unused;           /* A6-0x34: pea'd, never touched */

    /* Copy input UID, then seed the record's UID field at +0x08 */
    local_uid.high = file_uid->high;
    local_uid.low = file_uid->low;
    loc_rec.uid = local_uid;

    /*
     * Clear bit 6 of the record's flags byte at +0x1D.  This removes the
     * "local only" constraint, allowing the query to return remote
     * location information.
     */
    loc_rec.flags &= (int8_t)~FILE_OBJ_LOC_SCRATCH;

    /* Call AST_$GET_LOCATION to get the file's location */
    AST_$GET_LOCATION(&loc_rec, 0, &loc_unused, &vol_uid_out, &status);

    /* 0x00E60668 returns the longword at A6-0x0C, i.e. loc_rec+0x14. */
    *location_out = loc_rec.node;
    *status_ret = status;
}
