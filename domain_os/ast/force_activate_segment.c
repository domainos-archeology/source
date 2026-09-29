/*
 * ast_$force_activate_segment - Activate an object: find or build its AOTE
 *
 * Takes a fresh AOTE, then re-checks the hash chain if any other AOTE was
 * created while allocating (AST_$AOTE_SEQN moved): a matching entry that
 * is not in transition is returned after giving the fresh one back.
 * Otherwise the fresh AOTE is initialised from `uid` and `location`,
 * threaded onto the hash chain and, with the AST lock released, its
 * attributes are fetched: by volume search (force), by hint search
 * (location unknown), from the partner (remote) or from the VTOC
 * (local, followed by VTOCE_$READ).  A volume that is dismounting turns
 * the result into ast_$validate_uid's verdict.  With the lock retaken,
 * success clears the in-transition bit; failure unlinks and releases the
 * AOTE and returns NULL.
 *
 * Parameters (frame at 0x00E020FA, `link.w A6,-0x18`):
 *   uid      (0x08,A6)  object UID (D5)
 *   location (0x0C,A6)  the location word; bit 31 = remote (bits 0..19
 *                       node, 20..30 network), else the low byte is the
 *                       volume index; zero = unknown.  Read nine times,
 *                       and its ADDRESS is handed to AST_$LOOKUP_WITH_HINTS,
 *                       which writes the resolved location back.
 *   status   (0x10,A6)  (A4)
 *   force    (0x14,A6)  a single byte (D2b): TRUE = search every volume
 *
 * Returns the AOTE in A0, or NULL.
 *
 * A5 is inherited (no `lea` here; every caller is AST code): 0x434 is
 * AST_$AOTE_SEQN, 0x420 AST_$DATA.vol_info_count, 0x428 AST_$AST_IN_TRANS_EC,
 * and (0x0,A5,D3w) indexes the AOTH at 0xE1DC80.
 *
 * Original address: 0x00E020FA (658 bytes).
 */

#include "ast/ast_internal.h"

/* The AOTE hash table, `AOTH` in the SAU2 map, is AST_$DATA.aoth (ast/ast.h). */

/*
 * UID_$HASH's table-size word: `pea (-0x52e,PC)` at 0x00E02118 ->
 * 0x00E01BEC, image bytes 00 FB (251 buckets).  The same cell is used by
 * ast_$lookup_aote_by_uid, ast_$process_aote and AST_$LOOKUP_WITH_HINTS.
 */
static const uint16_t ast_$aoth_hash_size_00e01bec = 0x00FB;

/*
 * NETWORK_$AST_GET_INFO's request-flags word: `pea (-0x512,PC)` at
 * 0x00E02274 -> 0x00E01D64, image bytes 00 08.  Shared with
 * AST_$LOOKUP_WITH_HINTS (0x00E01CF2).
 */
static const uint16_t ast_$net_info_flags_00e01d64 = 0x0008;

aote_t *ast_$force_activate_segment(uid_t *uid, uint32_t location,
                                    status_$t *status, int8_t force)
{
    aote_t *aote;               /* A3: the fresh AOTE */
    aote_t *existing;           /* A2 */
    aote_t *prev;               /* A0 in the unlink loop */
    uint32_t seqn_before;       /* D3 */
    uint16_t hash_index;        /* D4w */
    uint16_t vol_idx;           /* D0w */

    /* 0x00E0210E..0x00E02116 */
    seqn_before = AST_$AOTE_SEQN;
    aote = ast_$allocate_aote();

    /* 0x00E02118..0x00E02126 */
    hash_index = (uint16_t)UID_$HASH(uid,
                                     (uint16_t *)&ast_$aoth_hash_size_00e01bec);

    /*
     * 0x00E02128..0x00E02168: if the sequence number moved, look for an
     * AOTE another activation may have made for this UID.  A match that
     * is in transition is waited for and the whole test repeated.
     */
    while (seqn_before != AST_$AOTE_SEQN) {
        existing = AST_$DATA.aoth[hash_index];
        while (existing != NULL) {
            /* 0x00E02138..0x00E02146: two cmpm.l over aote+0x10 */
            if (existing->uid.high == uid->high &&
                existing->uid.low == uid->low) {
                /* 0x00E02148: tst.b (0xbf,A2) */
                if ((int8_t)existing->flags >= 0) {
                    /* 0x00E02154..0x00E0215E */
                    ast_$release_aote(aote);
                    *status = status_$ok;
                    return existing;
                }
                /* 0x00E0214E..0x00E02152 */
                AST_$WAIT_FOR_AST_INTRANS();
                break;
            }
            existing = existing->hash_next;                 /* 0x00E02162 */
        }
        if (existing == NULL) {
            break;
        }
    }

    /* 0x00E0216A */
    AST_$AOTE_SEQN++;

    /*
     * 0x00E0216E..0x00E02180: one bset and three bclr on the flags byte;
     * bits 0..3 carry over from the recycled AOTE.
     */
    aote->flags |= AOTE_FLAG_IN_TRANS;
    aote->flags &= (uint8_t)~AOTE_FLAG_BUSY;
    aote->flags &= (uint8_t)~AOTE_FLAG_DIRTY;
    aote->flags &= (uint8_t)~AOTE_FLAG_TOUCHED;
    /* 0x00E02186..0x00E02194 */
    aote->ref_count = 0;
    aote->status_flags = 0;
    aote->hash_next = NULL;
    aote->aste_list = NULL;
    aote->location = location;

    /* 0x00E0219A..0x00E021A0: the caller's UID */
    aote->uid.high = uid->high;
    aote->uid.low = uid->low;

    /* 0x00E021A4: clr.b (0x9c,A3) - only the first byte of the embedded
     * file_$obj_loc_t (the high byte of its reserved_00 word) */
    aote->obj_uid.high &= 0x00FFFFFFu;

    /* 0x00E021A8..0x00E021AE: obj_loc.uid := the caller's UID */
    aote->obj_loc_uid.high = uid->high;
    aote->obj_loc_uid.low = uid->low;

    /*
     * 0x00E021B2..0x00E021C6: `tst.w (0xc,A6)` / `smi` tests the sign of
     * the location's HIGH word, i.e. bit 31; it becomes bit 7 of
     * obj_loc.flags (aote+0xB9) with the other bits kept, then bit 6 is
     * cleared.
     */
    aote->remote_flag = (int8_t)((aote->remote_flag & 0x7F) |
                                 (((int32_t)location < 0) ? 0x80 : 0x00));
    aote->remote_flag &= (int8_t)~0x40;

    /* 0x00E021CC..0x00E02204 */
    if ((int32_t)location < 0) {
        /* remote: no volume; node id = low 20 bits; ask NETWORK for the
         * network id of this location (pushes: status, &obj_loc_net,
         * location) */
        aote->vol_index = 0;
        aote->obj_loc_node = location & 0xFFFFF;
        NETWORK_$GET_NET(location, &aote->obj_loc_net, status);
    } else {
        /* local: the low byte of the location is the volume index */
        aote->vol_index = (uint8_t)(location & 0x7FFFFFFF);
    }

    /* 0x00E02208..0x00E02210: push onto the head of the bucket */
    aote->hash_next = AST_$DATA.aoth[hash_index];
    AST_$DATA.aoth[hash_index] = aote;

    /* 0x00E02214..0x00E02220 */
    ML_$UNLOCK(AST_LOCK_ID);

    /* 0x00E02222..0x00E0222C: a location with no low bits is unknown */
    if ((location & 0x7FFFFFFF) == 0) {
        /* 0x00E0222E */
        if (force < 0) {
            /* 0x00E02232..0x00E0223C -> 0x00E022A8 */
            VTOC_$SEARCH_VOLUMES(&aote->obj_uid, status);
            goto check_lookup_status;
        }
        /*
         * 0x00E0223E..0x00E02250: pushes status, &attrs (aote+0x0C),
         * &location, &obj_loc (aote+0x9C).  On success the hint search
         * writes the resolved remote location back through &location.
         */
        AST_$LOOKUP_WITH_HINTS(&aote->obj_uid, &location,
                               &aote->obj_type, status);
        if (*status != status_$ok) {                       /* 0x00E02254 */
            goto relock;
        }
        if (aote->remote_flag < 0) {                       /* 0x00E0225A */
            aote->location = location;                     /* 0x00E02260 */
            goto after_location_stored;
        }
        goto store_block_hint;                             /* -> 0x00E022AE */
    }

    /* 0x00E02268: the location is known */
    if (aote->remote_flag < 0) {
        /* 0x00E0226E..0x00E02286: pushes status, &attrs, &flags word,
         * &obj_loc; no status test before 0x00E022B4 */
        NETWORK_$AST_GET_INFO(&aote->obj_uid,
                              (uint16_t *)&ast_$net_info_flags_00e01d64,
                              &aote->obj_type, status);
        goto after_location_stored;
    }

    /*
     * 0x00E02288..0x00E0229A: a local volume index of at most 15 whose bit
     * is set in AST_$DATA.vol_info_count is dismounting (`btst.l D0,D1` /
     * `bhi`: C is clear from the preceding cmp, so bhi means "bit set").
     */
    vol_idx = aote->vol_index;
    if (vol_idx <= 0xF && (AST_$DATA.vol_info_count & (1u << vol_idx)) != 0) {
        goto bad_volume;
    }
    /* 0x00E0229C..0x00E022A2 */
    VTOC_$LOOKUP((vtoc_$lookup_req_t *)(void *)&aote->obj_uid, status);

check_lookup_status:
    /* 0x00E022A8..0x00E022AC */
    if (*status != status_$ok) {
        goto relock;
    }

store_block_hint:
    /* 0x00E022AE: for a located local object the location word becomes
     * obj_loc.block_hint (aote+0xA0) */
    aote->location = aote->obj_uid.low;

after_location_stored:
    /* 0x00E022B4: remote objects have no VTOCE to read */
    if (aote->remote_flag < 0) {
        goto relock;
    }
    /* 0x00E022BA..0x00E022CC: same dismount test (`bls` = bit clear) */
    vol_idx = aote->vol_index;
    if (vol_idx <= 0xF && (AST_$DATA.vol_info_count & (1u << vol_idx)) != 0) {
        goto bad_volume;
    }
    /* 0x00E022E0..0x00E022F0: pushes status, &attrs, &obj_loc */
    VTOCE_$READ((vtoc_$lookup_req_t *)(void *)&aote->obj_uid,
                (vtoce_$result_t *)(void *)&aote->obj_type, status);
    /* 0x00E022F4..0x00E022FC: attr_flags_lo bit 1 -> blocks := 0 */
    if (aote->attr_flags_lo & 0x02) {
        aote->blocks = 0;
    }
    goto relock;

bad_volume:
    /* 0x00E022CE..0x00E022DC: pushes 0x30F00, uid */
    *status = ast_$validate_uid(uid, 0x30F00);

relock:
    /* 0x00E02300..0x00E0230C */
    ML_$LOCK(AST_LOCK_ID);

    /* 0x00E0230E..0x00E02336: the volume may have started dismounting
     * while the lock was released */
    if (aote->remote_flag >= 0) {
        vol_idx = aote->vol_index;
        if (vol_idx <= 0xF && (AST_$DATA.vol_info_count & (1u << vol_idx)) != 0) {
            *status = ast_$validate_uid(uid, 0x30F00);
        }
    }

    /* 0x00E02338..0x00E0233A */
    if (*status == status_$ok) {
        /* 0x00E02370..0x00E02380 */
        aote->flags &= (uint8_t)~AOTE_FLAG_IN_TRANS;
        EC_$ADVANCE(&AST_$AST_IN_TRANS_EC);
        return aote;
    }

    /* 0x00E0233C..0x00E0234E: "UID not found" is re-judged by
     * ast_$validate_uid (pushes status, uid) */
    if (*status == 0x20006) {
        *status = ast_$validate_uid(uid, *status);
    }

    /* 0x00E02350..0x00E02364: unlink from the bucket */
    prev = AST_$DATA.aoth[hash_index];
    if (prev == aote) {
        AST_$DATA.aoth[hash_index] = aote->hash_next;
    } else {
        while (prev->hash_next != aote) {
            prev = prev->hash_next;
        }
        prev->hash_next = aote->hash_next;
    }

    /* 0x00E02366..0x00E0236E */
    ast_$release_aote(aote);
    return NULL;
}
