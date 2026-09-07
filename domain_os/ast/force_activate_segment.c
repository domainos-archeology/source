/*
 * ast_$force_activate_segment - Force lookup/create AOTE for an object
 *
 * Looks up or creates an AOTE for the given UID. If the object doesn't
 * exist in the cache, allocates a new AOTE and loads the object info.
 *
 * Parameters:
 *   uid      - Object UID to activate
 *   location - the object's location word (A6+0x0C, a longword).  Bit 31 set
 *              means remote: bits 0..19 are the node id and bits 20..30 the
 *              network number.  Bit 31 clear means local: the low byte is the
 *              logical volume index.  Zero means "location unknown", which
 *              selects the hint search instead of a direct VTOC lookup.  The
 *              encoding and the nine reads of this argument are documented on
 *              aote_t.location in ast/ast.h and on the prototype in
 *              ast/ast_internal.h (bead source-sy5u).
 *   status   - Output status
 *   force    - Force flag (negative = force activation)
 *
 * Returns: Pointer to AOTE (or NULL on error)
 *
 * Bead source-xntu turned every AOTE access in this routine into a named
 * aote_t field; the 0xA4..0xB7 region was recovered from the stores below
 * and is written up on aote_t.obj_loc_uid / obj_loc_net / obj_loc_node /
 * obj_loc_res_18 in ast/ast.h.  aote_t 0x9C..0xBB is the object's embedded
 * file_$obj_loc_t, so `aote->obj_uid.low` below is that record's block_hint
 * at aote+0xA0 rather than half of a UID.
 *
 * Original address: 0x00e020fa
 */

#include "ast/ast_internal.h"

/* Network info flags */
#if defined(ARCH_M68K)
#define AST_HASH_TABLE_INFO  ((void *)0xE01BEC)
#define NET_INFO_FLAGS       ((void *)0xE01D64)
#define AST_AOTH_BASE        ((aote_t **)0xE1DC80)
#define AST_$AOTE_SEQN       (*(uint32_t *)0xE1E0B4)
#else
#define AST_HASH_TABLE_INFO  ast_hash_table_info
#define NET_INFO_FLAGS       net_info_flags
#define AST_AOTH_BASE        ast_aoth_base
#define AST_$AOTE_SEQN       ast_$aote_seqn
#endif

aote_t *ast_$force_activate_segment(uid_t *uid, uint32_t location,
                                    status_$t *status, int8_t force)
{
    aote_t *aote;
    aote_t *existing;
    uint32_t seqn_before;
    uint16_t hash_index;

    seqn_before = AST_$AOTE_SEQN;

    /* Allocate a new AOTE */
    aote = ast_$allocate_aote();

    /* Hash the UID */
    hash_index = UID_$HASH(uid, (uint16_t *)AST_HASH_TABLE_INFO);

    /* Check if another AOTE was created for this UID while we were allocating */
    while (seqn_before != AST_$AOTE_SEQN) {
        /* Search hash chain for existing entry */
        existing = AST_AOTH_BASE[hash_index];
        while (existing != NULL) {
            /* 0x00E02138-0x00E02146: `lea (0x10,A2),A0` and two `cmpm.l`. */
            if (existing->uid.high == uid->high &&
                existing->uid.low == uid->low) {
                /* Found existing - check if in-transition */
                if ((int8_t)existing->flags >= 0) {   /* 0x00E02148: tst.b (0xbf,A2) */
                    /* Not in transition - release our AOTE and return existing */
                    ast_$release_aote(aote);
                    *status = status_$ok;
                    return existing;
                }
                /* In transition - wait */
                AST_$WAIT_FOR_AST_INTRANS();
                break;
            }
            existing = existing->hash_next;
        }
        if (existing == NULL) {
            break;
        }
    }

    /* Initialize the new AOTE */
    AST_$AOTE_SEQN++;

    /*
     * 0x00E0216E-0x00E02180: one bset and three bclr, so bits 0..3 of the
     * flags byte are carried over from the recycled AOTE rather than cleared.
     */
    aote->flags |= AOTE_FLAG_IN_TRANS;                  /* 0x00E0216E: bset.b #7 */
    aote->flags &= (uint8_t)~AOTE_FLAG_BUSY;            /* 0x00E02174: bclr.b #6 */
    aote->flags &= (uint8_t)~AOTE_FLAG_DIRTY;           /* 0x00E0217A: bclr.b #5 */
    aote->flags &= (uint8_t)~AOTE_FLAG_TOUCHED;         /* 0x00E02180: bclr.b #4 */
    aote->ref_count = 0;                                /* 0x00E02186 */
    aote->status_flags = 0;                             /* 0x00E0218A */
    aote->hash_next = NULL;                             /* 0x00E0218E */
    aote->aste_list = NULL;                             /* 0x00E02190 */
    /* 0x00E02194: the location word is cached in the AOTE as-is. */
    aote->location = location;

    /* 0x00E0219C-0x00E021A0: the caller's UID, two post-increment moves. */
    aote->uid.high = uid->high;
    aote->uid.low = uid->low;

    /*
     * 0x00E021A4: `clr.b (0x9c,A3)` clears only the FIRST byte of the
     * embedded file_$obj_loc_t, i.e. the high byte of its reserved_00 word.
     * Expressed as a mask so the meaning does not depend on byte order.
     */
    aote->obj_uid.high &= 0x00FFFFFFu;

    /* 0x00E021AA-0x00E021AE: obj_loc.uid := the caller's UID. */
    aote->obj_loc_uid.high = uid->high;
    aote->obj_loc_uid.low = uid->low;

    /*
     * 0x00E021B2-0x00E021C6: bit 31 of the location word becomes bit 7 of
     * obj_loc.flags (aote+0xB9), the other bits of that byte are preserved
     * and bit 6 is cleared unconditionally.  `tst.w (0xc,A6)` reads the HIGH
     * word of the longword on this big-endian machine, so the test is the
     * sign of the whole longword, not of its low half.
     */
    aote->remote_flag =
        (int8_t)((aote->remote_flag & 0x7F) |
                 (((int32_t)location < 0) ? 0x80 : 0x00));
    aote->remote_flag &= (int8_t)~0x40;

    if ((int32_t)location < 0) {
        /* Remote object - 0x00E021E2-0x00E02204 */
        aote->vol_index = 0;                            /* 0x00E021E2 */
        /* 0x00E021E6-0x00E021F0: obj_loc.node := location & 0xFFFFF */
        aote->obj_loc_node = location & 0xFFFFF;
        /* 0x00E021F6-0x00E021FE: obj_loc.loc_info := the network id */
        NETWORK_$GET_NET(location, &aote->obj_loc_net, status);
    } else {
        /* Local object - 0x00E021D2-0x00E021DC */
        aote->vol_index = (uint8_t)(location & 0x7FFFFFFF);
    }

    /* Insert into hash chain */
    hash_index = hash_index << 2;  /* Convert to byte offset */
    aote->hash_next = AST_AOTH_BASE[hash_index >> 2];
    AST_AOTH_BASE[hash_index >> 2] = aote;

    /* Release AST lock for I/O */
    ML_$UNLOCK(AST_LOCK_ID);

    /*
     * Load object info.  0x00E02222: a location word whose low 31 bits are
     * zero means the object's whereabouts are unknown, so the volume search
     * or the hint search runs instead of a direct lookup.
     */
    if ((location & 0x7FFFFFFF) == 0) {
        if (force < 0) {
            /* 0x00E02238 */
            VTOC_$SEARCH_VOLUMES(&aote->obj_uid, status);
            goto check_lookup_status;                  /* 0x00E0223C -> 0x00E022A8 */
        }
        /*
         * 0x00E02244 pushes the ADDRESS of the location word: on success
         * AST_$LOOKUP_WITH_HINTS writes the resolved remote location back
         * through it (0x00E01D08 sets bit 31, 0x00E01D2A merges the network
         * number and the node id).
         */
        AST_$LOOKUP_WITH_HINTS(&aote->obj_uid, &location,
                               &aote->obj_type, status);
        if (*status != status_$ok) {                   /* 0x00E02254 */
            goto relock_and_check;
        }
        if (aote->remote_flag < 0) {                   /* 0x00E0225A */
            /* 0x00E02260: re-cache the location the hint search resolved. */
            aote->location = location;
            goto after_location_stored;                /* 0x00E02266 -> 0x00E022B4 */
        }
        goto store_location_from_hint;                 /* falls into 0x00E022AE */
    }

    /* 0x00E02268: the location is known. */
    if (aote->remote_flag < 0) {
        /* Remote - 0x00E0227C, no status test before 0x00E022B4 */
        NETWORK_$AST_GET_INFO(&aote->obj_uid, NET_INFO_FLAGS,
                              &aote->obj_type, status);
        goto after_location_stored;                    /* 0x00E02286 */
    }

    /* Local - check volume status (0x00E02288) */
    {
        uint8_t vol_idx = aote->vol_index;             /* 0x00E0228C */
        if (vol_idx <= 0x0F) {
            uint16_t vol_flags = *((uint16_t *)(0xE1E0A0));  /* A5+0x420 */
            if ((vol_flags & (1 << vol_idx)) != 0) {
                goto bad_volume;                       /* 0x00E0229A -> 0x00E022CE */
            }
        }
    }
    VTOC_$LOOKUP((vtoc_$lookup_req_t *)(void *)&aote->obj_uid, status);

check_lookup_status:                                   /* 0x00E022A8 */
    if (*status != status_$ok) {
        goto relock_and_check;
    }
store_location_from_hint:                              /* 0x00E022AE */
    /*
     * For a located local object the location word becomes obj_loc.block_hint
     * (aote+0xA0, the low half of the `obj_uid` longword pair).
     */
    aote->location = aote->obj_uid.low;

after_location_stored:                                 /* 0x00E022B4 */
    if (aote->remote_flag < 0) {
        goto relock_and_check;
    }
    {
        /* Load VTOCE for local objects */
        uint8_t vol_idx = aote->vol_index;             /* 0x00E022BE */
        if (vol_idx <= 0x0F) {
            uint16_t vol_flags = *((uint16_t *)(0xE1E0A0));
            if ((vol_flags & (1 << vol_idx)) != 0) {
                goto bad_volume;                       /* 0x00E022CC -> 0x00E022CE */
            }
        }
    }
    VTOCE_$READ((vtoc_$lookup_req_t *)(void *)&aote->obj_uid,
                (vtoce_$result_t *)(void *)&aote->obj_type, status);

    /* Clear per-boot fields if object has them */
    if ((aote->attr_flags_lo & 2) != 0) {              /* 0x00E022F4 */
        aote->blocks = 0;                              /* 0x00E022FC */
    }
    goto relock_and_check;

bad_volume:                                            /* 0x00E022CE */
    *status = ast_$validate_uid(uid, 0x30F00);

relock_and_check:
    ML_$LOCK(AST_LOCK_ID);

    /* Re-check volume status after relock */
    if (aote->remote_flag >= 0) {                      /* 0x00E0230E */
        uint8_t vol_idx = aote->vol_index;             /* 0x00E02318 */
        if (vol_idx <= 0x0F) {
            uint16_t vol_flags = *((uint16_t *)(0xE1E0A0));
            if ((vol_flags & (1 << vol_idx)) != 0) {
                *status = ast_$validate_uid(uid, 0x30F00);
            }
        }
    }

    if (*status == status_$ok) {
        /* Success - clear in-transition */
        aote->flags &= (uint8_t)~AOTE_FLAG_IN_TRANS;   /* 0x00E02370 */
        EC_$ADVANCE(&AST_$AST_IN_TRANS_EC);
        return aote;
    }

    /* Error - check for specific error code */
    if (*status == 0x20006) {  /* file_$object_not_found variant */
        *status = ast_$validate_uid(uid, 0x20006);
    }

    /* Remove from hash chain */
    existing = AST_AOTH_BASE[hash_index >> 2];
    if (existing == aote) {
        AST_AOTH_BASE[hash_index >> 2] = aote->hash_next;
    } else {
        while (existing->hash_next != aote) {
            existing = existing->hash_next;
        }
        existing->hash_next = aote->hash_next;
    }

    /* Release the AOTE */
    ast_$release_aote(aote);
    return NULL;
}
