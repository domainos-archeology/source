/*
 * DIR_$OLD_VALIDATE_ROOT_ENTRY - Legacy validate replicated-root entry
 *
 * Original address: 0x00E580C4
 * Original size: 314 bytes (0x00E580C4-0x00E581FD)
 *
 * Re-derived from the disassembly (bead source-kurv).  A5 = 0x00E7FD24.
 */

#include "dir/dir_internal.h"

/*
 * 0x00E544AE, word 0x0020: the 32-byte output-buffer size the UNMAP_CASE call
 * at 0x00E5819E gets (`pea (-0x3cfa,PC)` at 0x00E581A6) is the shared cell
 * name_$leaf_max_len_00e544ae (name/name.h).
 */

/*
 * DIR_$OLD_VALIDATE_ROOT_ENTRY - Legacy validate replicated-root entry
 *
 * Parameters:
 *   name       - (0x08,A6) -> A2, the entry name in the root
 *   name_len   - (0x0c,A6) -> A3, pointer to the name length word
 *   status_ret - (0x10,A6) -> A4, output: status code
 *
 * Both directory arguments are the ADDRESSES of well-known UIDs pushed as
 * immediates: NAME_$ROOT_UID (0x00E8029C) for the three local calls and
 * NAME_$CANNED_REP_ROOT_UID (0x00E173FC) for the naming-server lookup.
 * There is no local copy of either.
 */
void DIR_$OLD_VALIDATE_ROOT_ENTRY(char *name, uint16_t *name_len,
                                  status_$t *status_ret)
{
    /* link.w A6,-0x74 */
    int8_t            truncated;        /* A6-0x74 */
    uint16_t          unmapped_len;     /* A6-0x72 */
    dir_$old_entry_t  local_entry;      /* A6-0x70 */
    dir_$rep_entry_t  rep_entry;        /* A6-0x58 */
    uint8_t           drop_result[8];   /* A6-0x28 */
    uint8_t           unmapped_name[32];/* A6-0x20 .. A6-0x01 */

    /* 0x00E580DE */
    name_$old_get_entry_nonroot(&NAME_$ROOT_UID, name, *name_len,
                                &local_entry, status_ret);
    if ((int16_t)*status_ret != 0) {    /* 0x00E580F8: tst.w (2,A4) */
        return;
    }

    /*
     * 0x00E58100: the naming server is asked about the CANNED replicated-root
     * UID, and it writes the caller's own status cell - whatever it leaves
     * there is what this routine returns.
     */
    REM_NAME_$GET_ENTRY(&NAME_$CANNED_REP_ROOT_UID, name, name_len,
                        &rep_entry, status_ret);
    if ((int16_t)*status_ret != 0) {    /* 0x00E5811A */
        return;
    }

    /* 0x00E58122-0x00E58132: the 8-byte UID compare */
    if (local_entry.uid.high == rep_entry.uid.high &&
        local_entry.uid.low  == rep_entry.uid.low) {
        /* 0x00E58134: the entries also have to agree on the extra longword */
        if (local_entry.extra == rep_entry.extra) {
            *status_ret = status_$ok;   /* 0x00E58172: clr.l (A4) */
            return;
        }
    } else {
        /*
         * 0x00E58140-0x00E58170: a UID mismatch is still acceptable when both
         * UIDs carry a nonzero leading byte, their low longwords agree in
         * their bottom 20 bits, and the local UID's high longword is
         * UNSIGNED-greater than the replicated one's.  That is the
         * "the local entry is the newer generation" case.
         */
        if ((uint8_t)(local_entry.uid.high >> 24) != 0 &&
            (uint8_t)(rep_entry.uid.high >> 24) != 0 &&
            (local_entry.uid.low & 0x000FFFFFu) ==
                (rep_entry.uid.low & 0x000FFFFFu) &&
            local_entry.uid.high > rep_entry.uid.high) {
            *status_ret = status_$ok;   /* 0x00E58172 */
            return;
        }
    }

    /* 0x00E58176: the cached entry is stale - drop it (type word 0) */
    name_$old_drop_entry(&NAME_$ROOT_UID, name, *name_len, 0,
                         drop_result, status_ret);
    if ((int16_t)*status_ret != 0) {    /* 0x00E58190 */
        *status_ret = status_$naming_cache_entry_stale;     /* 0x00E58196 */
        return;
    }

    /* 0x00E5819E: bring the replicated name back to Unix case */
    UNMAP_CASE((char *)rep_entry.name, (int16_t *)&rep_entry.name_len,
               (char *)unmapped_name, (int16_t *)&name_$leaf_max_len_00e544ae,
               (int16_t *)&unmapped_len, (uint8_t *)&truncated);

    /* 0x00E581BE: seven arguments - type word 0, the rep entry's own UID and
     * extra longword. */
    name_$old_add_entry(&NAME_$ROOT_UID, 0, (char *)unmapped_name,
                        unmapped_len, &rep_entry.uid, rep_entry.extra,
                        status_ret);
    if ((int16_t)*status_ret != 0) {    /* 0x00E581E0 */
        *status_ret = status_$naming_name_not_found;            /* 0x00E581E6 */
    } else {
        *status_ret = status_$naming_cache_entry_stale_and_updated; /* 0x00E581EE */
    }
}
