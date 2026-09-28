/*
 * name_$map_dir - Map a directory for fast access (0x00E58488, 212 bytes;
 * first routine of the SAU2 map's NAME segment at E58488, size 0x5B0)
 *
 * Copies the UID, asks AST_$GET_LOCATION whether the object is reachable,
 * then maps 0x10000 bytes of it with MST_$MAPS and records the mapping in
 * the caller's name_$mapped_info_t:
 *   +0x00  active       0xFF on success (cleared first thing)
 *   +0x02  reserved_02  0
 *   +0x04  first_base   MST_$MAPS' A0 result (stored before the status test)
 *   +0x0A  entry_count  1
 *   +0x0C  second_base  first_base + 0x8000
 *
 * Frame (A6+): 0x08 dir_uid, 0x0C asid (word), 0x0E mapped_info -> A2,
 * 0x12 status_ret -> A3.
 * Locals (A6-): -0x30 local_uid, -0x28 0x20-byte location record (UID at
 * -0x20, flags byte at -0x0B), -0x34 local status, -0x38 MST_$MAPS' length
 * result, -0x3C / -0x40 AST_$GET_LOCATION's two outputs.
 *
 * Returns D2b: 0xFF when the directory was mapped, else 0.
 */

#include "name/name_internal.h"
#include "mst/mst.h"
#include "ast/ast.h"

boolean name_$map_dir(uid_t *dir_uid, int16_t asid,
                      name_$mapped_info_t *mapped_info,
                      status_$t *status_ret)
{
    uid_t            local_uid;             /* A6-0x30 */
    file_$obj_loc_t  loc_rec;               /* A6-0x28 */
    status_$t        local_status;          /* A6-0x34 */
    uint32_t         map_len;               /* A6-0x38: MST_$MAPS' out buffer */
    uint32_t         loc_unused;            /* A6-0x3C: never read */
    uint32_t         location_info;         /* A6-0x40 */
    boolean          result;                /* D2b */
    void            *mapped_base;           /* A0 result of MST_$MAPS */

    /* 0x00E58498-0x00E584A0: local copy of the UID */
    local_uid.high = dir_uid->high;
    local_uid.low  = dir_uid->low;

    mapped_info->active = 0;                                /* 0x00E584A4 clr.b (A2) */
    result = 0;                                             /* 0x00E584AA clr.b D2b */

    /* 0x00E584AC-0x00E584B4: seed the location record's UID (+0x08) and
     * clear bit 6 of its flags byte (+0x1D); the rest is uninitialised. */
    loc_rec.uid = local_uid;
    loc_rec.flags = (int8_t)(loc_rec.flags & (int8_t)~FILE_OBJ_LOC_SCRATCH);

    /* 0x00E584BA-0x00E584D4: AST_$GET_LOCATION(&loc_rec, 0, &-0x3C, &-0x40,
     * &local_status) */
    AST_$GET_LOCATION(&loc_rec, 0, &loc_unused, &location_info, &local_status);

    /* 0x00E584D8-0x00E584E8: a bad status, or a negative HIGH word of
     * location_info (`tst.w (-0x40,A6)` = bit 31), hands the local status
     * back and returns false. */
    if (local_status != status_$ok || (int32_t)location_info < 0) {
        *status_ret = local_status;
        return result;                                      /* 0x00E58550 */
    }

    /* 0x00E584EA-0x00E58510: MST_$MAPS(asid, true, &local_uid, 0, 0x10000,
     * 0x16, 0, true, &map_len, status_ret).  The two `st -(SP)` pushes are
     * Pascal booleans read as bytes by the callee. */
    mapped_base = MST_$MAPS(asid, true, &local_uid, 0, 0x10000, 0x16, 0, true,
                            &map_len, status_ret);

    /* 0x00E58514: the A0 result goes into +0x04 BEFORE the status is looked
     * at, so a failed map still leaves the address behind. */
    mapped_info->first_base = ARCH_PTR_TO_VA(mapped_base);

    /* 0x00E5851C `tst.w (0x2,A3)`: the LOW word of the status. */
    if ((uint16_t)((uint32_t)*status_ret & 0xFFFFu) != 0) {
        return result;                                      /* 0x00E58550 */
    }

    /* 0x00E58522-0x00E5852E: anything but a 0x10000-byte map is fatal;
     * 0xE5855C holds status 0x000E0025 (Naming_Internal_Err). */
    if (map_len != 0x10000) {
        CRASH_SYSTEM(&Naming_Internal_Err);
    }

    /* 0x00E58534-0x00E5854C */
    mapped_info->active = (int8_t)-1;
    result = (boolean)-1;
    mapped_info->reserved_02 = 0;
    mapped_info->entry_count = 1;
    mapped_info->second_base = mapped_info->first_base + 0x8000;

    return result;                                          /* 0x00E58550 */
}
