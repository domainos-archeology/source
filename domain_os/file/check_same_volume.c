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
 *   file_uid1     - First file UID                   (A6+0x08)
 *   file_uid2     - Second file UID (from ACL source) (A6+0x0C)
 *   copy_location - Pascal boolean; when true the remote path hands the
 *                   caller the whole 0x20-byte record  (A6+0x10, byte)
 *   location_out  - Output buffer for the location record (A6+0x12)
 *   status_ret    - Output status code                (A6+0x16)
 *
 * Returns:
 *   D0.B - Pascal boolean: true (-1) when both objects are local and their
 *          location records agree, false (0) otherwise.
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
    uid_t uid1_raw;                 /* A6-0x68: caller's UID, unmodified   */
    uid_t uid2_raw;                 /* A6-0x60 */
    uid_t uid1_masked;              /* A6-0x18: high nibble of low[31:24]  */
    uid_t uid2_masked;              /* A6-0x10 */
    int32_t dism_seqn_start, dism_seqn_end;
    status_$t location_status;      /* A6-0x6c */
    int8_t result;                  /* D2.B    */

    /*
     * The two 0x20-byte object-location records AST_$GET_LOCATION fills
     * (A6-0x58 and A6-0x38).  The UID goes in at +0x08 and the whole record
     * is overwritten from aote+0x9C on success.
     */
    file_$obj_loc_t loc1;
    file_$obj_loc_t loc2;

    /*
     * A6-0x78 / A6-0x74: the two 4-byte cells both AST_$GET_LOCATION calls
     * are handed (`pea (-0x78,A6)` / `pea (-0x74,A6)` at 0x00E5E4E4).  The
     * first is the argument the routine never touches; the second receives
     * aote+0x08.  Both calls share the same pair of cells, and neither value
     * is read afterwards.
     */
    uint32_t get_location_unused;
    uint32_t vol_uid;

    /* Copy the caller's UIDs, then mask the top byte of the low longword.
     * 0x00E5E4AA: `andi.b #-0x10,(-0x14,A6)` - the byte at +4 of the UID is
     * uid.low bits 31..24 on the big-endian m68k. */
    uid1_raw = *file_uid1;
    uid2_raw = *file_uid2;

    uid1_masked = uid1_raw;
    uid1_masked.low &= 0xF0FFFFFFu;
    uid2_masked = uid2_raw;
    uid2_masked.low &= 0xF0FFFFFFu;

    do {
        result = 0;                                     /* 0x00E5E4C2 */

        /* Get dismount sequence number to detect changes */
        dism_seqn_start = AST_$GET_DISM_SEQN();

        /* Set up UID for first lookup at offset 8 in the record */
        loc1.uid = uid1_masked;                         /* 0x00E5E4D0 */

        /* Clear the scratch flag before the lookup (bclr.b #6,(-0x3b,A6)) */
        loc1.flags &= (int8_t)~FILE_OBJ_LOC_SCRATCH;

        /* Get location of first file */
        AST_$GET_LOCATION(&loc1, 0, &get_location_unused, &vol_uid,
                          &location_status);

        if (location_status != status_$ok) {
            goto done;
        }

        /* Check if first file is remote (tst.b (-0x3b,A6); bpl) */
        if (loc1.flags < 0) {
            if (copy_location < 0) {
                /* 0x00E5E50E: copy all 8 longwords of the record out */
                int16_t i;
                const uint32_t *src = (const uint32_t *)(const void *)&loc1;
                for (i = 7; i >= 0; i--) {
                    *location_out++ = *src++;
                }
                *status_ret = file_$object_is_remote;
                return 0;
            }

            /*
             * 0x00E5E526: REM_FILE_$NEIGHBORS(&loc1.loc_info, &uid1_masked,
             *                                 &uid2_masked, &location_status)
             * The MASKED UIDs are passed (pea (-0x18,A6) / pea (-0x10,A6)),
             * and the record pointer is the record base + 0x10.
             */
            result = REM_FILE_$NEIGHBORS(&loc1.loc_info,
                                         &uid1_masked, &uid2_masked,
                                         &location_status);
            goto done;
        }

        /* First file is local, check second file */
        loc2.uid = uid2_masked;                         /* 0x00E5E544 */
        loc2.flags &= (int8_t)~FILE_OBJ_LOC_SCRATCH;    /* 0x00E5E54C */

        AST_$GET_LOCATION(&loc2, 1, &get_location_unused, &vol_uid,
                          &location_status);

        if (location_status != status_$ok) {
            goto done;
        }

        /*
         * 0x00E5E578: compare the WORD at offset +0x02 of each record
         * (`move.w (-0x56,A6),D2w` / `cmp.w (-0x36,A6),D2w`) and AND that
         * with "record 2 is not remote" (spl on the flags byte).  On the
         * big-endian m68k the word at +0x02 is the low half of the longword
         * at +0x00.
         */
        result = (int8_t)(((loc1.reserved_00[0] & 0xFFFFu) ==
                           (loc2.reserved_00[0] & 0xFFFFu) ? -1 : 0) &
                          (loc2.flags >= 0 ? -1 : 0));

        /* Check if dismount sequence changed */
        dism_seqn_end = AST_$GET_DISM_SEQN();

    } while (dism_seqn_end != dism_seqn_start);

    location_status = status_$ok;                       /* 0x00E5E59A */

done:
    *status_ret = location_status;
    return result;
}
