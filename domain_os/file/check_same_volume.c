/*
 * FILE_$CHECK_SAME_VOLUME - Check if two files are on the same volume
 *
 * Checks if two file UIDs refer to objects on the same volume.
 * Used during protection operations to verify source/target locations.
 *
 * Original address: 0x00E5E476
 */

#include "file/file_internal.h"

/* Status codes */
#define file_$object_is_remote               0x000F0002
#define file_$objects_on_different_volumes   0x000F0013

/*
 * FILE_$CHECK_SAME_VOLUME
 *
 * Checks if two file UIDs are on the same volume. Handles remote files
 * by optionally copying location info for the caller.
 *
 * Parameters:
 *   file_uid1     - First file UID
 *   file_uid2     - Second file UID (from ACL source)
 *   copy_location - If true (negative), copy location info on remote hit
 *   location_out  - Output buffer for location info (32 bytes)
 *   status_ret    - Output status code
 *
 * Returns:
 *   0: Files are on different volumes or error occurred
 *   -1: Files are local and on different logical volumes
 *   Positive: Files are on same volume
 *
 * Flow:
 * 1. Get dismount sequence number (for consistency check)
 * 2. Get location of file1
 * 3. If file1 is remote:
 *    - If copy_location is set, copy location info and return remote status
 *    - Otherwise call REM_FILE_$NEIGHBORS to get neighbor info
 * 4. If file1 is local, get location of file2
 * 5. Compare volume identifiers
 * 6. Repeat if dismount sequence changed during operation
 */
int8_t FILE_$CHECK_SAME_VOLUME(uid_t *file_uid1, uid_t *file_uid2,
                                int8_t copy_location, uint32_t *location_out,
                                status_$t *status_ret)
{
    uint32_t uid1_high, uid1_low;
    uint32_t uid2_high, uid2_low;
    uint32_t uid1_masked_low, uid2_masked_low;
    int32_t dism_seqn_start, dism_seqn_end;
    status_$t location_status;
    int8_t result;

    /*
     * Location info structures - AST_$GET_LOCATION expects:
     *   - UID at offset 8 (bytes 8-15) as INPUT
     *   - Writes 32 bytes of location info back to the buffer
     */
    struct {
        uint32_t data[2];       /* 8 bytes - location output starts here */
        uid_t    uid;           /* 8 bytes - UID input at offset 8 */
        uint32_t data2[4];      /* 16 bytes - more location output */
        uint8_t  pad[5];
        uint8_t  remote_flags;  /* Bit 7 set if remote */
    } loc_info1;

    struct {
        uint32_t data[2];       /* 8 bytes */
        uid_t    uid;           /* 8 bytes - UID input at offset 8 */
        uint16_t volume_id;
        uint8_t  pad[3];
        uint8_t  remote_flags;
    } loc_info2;

    uint32_t vol_uid1;
    uint32_t vol_uid2;
    uid_t neighbor_uid1;
    uid_t neighbor_uid2;

    /* Copy and mask UIDs (clear type nibble from low word high byte) */
    uid1_high = file_uid1->high;
    uid1_low = file_uid1->low;
    uid2_high = file_uid2->high;
    uid2_low = file_uid2->low;

    uid1_masked_low = uid1_low & 0xF0FFFFFF;
    uid2_masked_low = uid2_low & 0xF0FFFFFF;

    do {
        result = 0;

        /* Get dismount sequence number to detect changes */
        dism_seqn_start = AST_$GET_DISM_SEQN();

        /* Set up UID for first lookup at offset 8 in the structure */
        loc_info1.uid.high = uid1_high;
        loc_info1.uid.low = uid1_masked_low;

        /* Clear remote flag before lookup */
        loc_info1.remote_flags &= ~0x40;

        /* Get location of first file */
        AST_$GET_LOCATION((uint32_t *)&loc_info1, 0, 0, &vol_uid1, &location_status);

        if (location_status != status_$ok) {
            goto done;
        }

        /* Check if first file is remote */
        if ((int8_t)loc_info1.remote_flags < 0) {  /* Bit 7 set */
            if (copy_location < 0) {
                /* Copy location info for caller */
                int16_t i;
                uint32_t *src = loc_info1.data;
                for (i = 7; i >= 0; i--) {
                    *location_out++ = *src++;
                }
                *status_ret = file_$object_is_remote;
                return 0;
            }

            /* Get neighbor info for remote file */
            neighbor_uid1.high = uid1_high;
            neighbor_uid1.low = uid1_low;
            neighbor_uid2.high = uid2_high;
            neighbor_uid2.low = uid2_low;
            result = REM_FILE_$NEIGHBORS(loc_info1.data,
                                         &neighbor_uid1, &neighbor_uid2,
                                         &location_status);
            goto done;
        }

        /* First file is local, check second file */
        loc_info2.uid.high = uid2_high;
        loc_info2.uid.low = uid2_masked_low;

        loc_info2.remote_flags &= ~0x40;

        AST_$GET_LOCATION((uint32_t *)&loc_info2, 1, 0, &vol_uid2, &location_status);

        if (location_status != status_$ok) {
            goto done;
        }

        /*
         * Compare volume identifiers.
         * Both files must be local (bit 7 clear) and have matching volume IDs.
         */
        result = ((int8_t)loc_info2.remote_flags >= 0) &&
                 (vol_uid1 == vol_uid2) ? -1 : 0;

        /* Check if dismount sequence changed */
        dism_seqn_end = AST_$GET_DISM_SEQN();

    } while (dism_seqn_end != dism_seqn_start);

    location_status = status_$ok;

done:
    *status_ret = location_status;
    return result;
}
