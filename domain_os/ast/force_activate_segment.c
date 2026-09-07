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
 *              aote_t.vol_uid in ast/ast.h and on the prototype in
 *              ast/ast_internal.h (bead source-sy5u).
 *   status   - Output status
 *   force    - Force flag (negative = force activation)
 *
 * Returns: Pointer to AOTE (or NULL on error)
 *
 * Original address: 0x00e020fa
 */

#include "ast/ast_internal.h"

/* External function prototypes */

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
    status_$t local_status;

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
            uint32_t *exist_uid = (uint32_t *)((char *)existing + 0x10);
            if (exist_uid[0] == uid->high && exist_uid[1] == uid->low) {
                /* Found existing - check if in-transition */
                if (*((int8_t *)((char *)existing + 0xBF)) >= 0) {
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

    /* Set in-transition flag */
    aote->flags = AOTE_FLAG_IN_TRANS;
    aote->flags &= ~(AOTE_FLAG_BUSY | AOTE_FLAG_DIRTY | AOTE_FLAG_TOUCHED);
    aote->ref_count = 0;
    aote->status_flags = 0;
    aote->hash_next = NULL;
    aote->aste_list = NULL;
    /* 0x00E02194: the location word is cached in the AOTE as-is. */
    *((uint32_t *)((char *)aote + 0x08)) = location;

    /* Copy UID to AOTE (offset 0x10 and 0x14) */
    *((uint32_t *)((char *)aote + 0x10)) = uid->high;
    *((uint32_t *)((char *)aote + 0x14)) = uid->low;

    /* Initialize UID info area (offset 0x9C) */
    *((uint8_t *)((char *)aote + 0x9C)) = 0;
    *((uint32_t *)((char *)aote + 0xA4)) = uid->high;
    *((uint32_t *)((char *)aote + 0xA8)) = uid->low;

    /*
     * 0x00E021B2-0x00E021C6: bit 31 of the location word becomes bit 7 of
     * aote+0xB9, the other bits of that byte are preserved and bit 6 is
     * cleared unconditionally.  `tst.w (0xc,A6)` reads the HIGH word of the
     * longword on this big-endian machine, so the test is the sign of the
     * whole longword, not of its low half.
     */
    *((uint8_t *)((char *)aote + 0xB9)) =
        (uint8_t)((*((uint8_t *)((char *)aote + 0xB9)) & 0x7F) |
                  (((int32_t)location < 0) ? 0x80 : 0x00));
    *((uint8_t *)((char *)aote + 0xB9)) &= (uint8_t)~0x40;

    if ((int32_t)location < 0) {
        /* Remote object - 0x00E021E2-0x00E02204 */
        *((uint8_t *)((char *)aote + 0xB8)) = 0;     /* Clear vol index */
        *((uint32_t *)((char *)aote + 0xB0)) = location & 0xFFFFF;  /* Node id */
        NETWORK_$GET_NET(location, (uint32_t *)((char *)aote + 0xAC), status);
    } else {
        /* Local object - 0x00E021D2-0x00E021DC */
        *((uint8_t *)((char *)aote + 0xB8)) = (uint8_t)(location & 0x7FFFFFFF);
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
            VTOC_$SEARCH_VOLUMES((char *)aote + 0x9C, status);
            goto check_lookup_status;                  /* 0x00E0223C -> 0x00E022A8 */
        }
        /*
         * 0x00E02244 pushes the ADDRESS of the location word: on success
         * AST_$LOOKUP_WITH_HINTS writes the resolved remote location back
         * through it (0x00E01D08 sets bit 31, 0x00E01D2A merges the network
         * number and the node id).
         */
        AST_$LOOKUP_WITH_HINTS((char *)aote + 0x9C, &location,
                    (char *)aote + 0x0C, status);
        if (*status != status_$ok) {                   /* 0x00E02254 */
            goto relock_and_check;
        }
        if (*((int8_t *)((char *)aote + 0xB9)) < 0) {  /* 0x00E0225A */
            /* 0x00E02260: re-cache the location the hint search resolved. */
            *((uint32_t *)((char *)aote + 0x08)) = location;
            goto after_location_stored;                /* 0x00E02266 -> 0x00E022B4 */
        }
        goto store_location_from_hint;                 /* falls into 0x00E022AE */
    }

    /* 0x00E02268: the location is known. */
    if (*((int8_t *)((char *)aote + 0xB9)) < 0) {
        /* Remote - 0x00E0227C, no status test before 0x00E022B4 */
        NETWORK_$AST_GET_INFO((char *)aote + 0x9C, NET_INFO_FLAGS,
                             (char *)aote + 0x0C, status);
        goto after_location_stored;                    /* 0x00E02286 */
    }

    /* Local - check volume status (0x00E02288) */
    {
        uint8_t vol_idx = *((uint8_t *)((char *)aote + 0xB8));
        if (vol_idx <= 0x0F) {
            uint16_t vol_flags = *((uint16_t *)(0xE1E0A0));  /* A5+0x420 */
            if ((vol_flags & (1 << vol_idx)) != 0) {
                goto bad_volume;                       /* 0x00E0229A -> 0x00E022CE */
            }
        }
    }
    VTOC_$LOOKUP((vtoc_$lookup_req_t *)((char *)aote + 0x9C), status);

check_lookup_status:                                   /* 0x00E022A8 */
    if (*status != status_$ok) {
        goto relock_and_check;
    }
store_location_from_hint:                              /* 0x00E022AE */
    /* For a located local object the location word becomes obj_loc.block_hint. */
    *((uint32_t *)((char *)aote + 0x08)) = *((uint32_t *)((char *)aote + 0xA0));

after_location_stored:                                 /* 0x00E022B4 */
    if (*((int8_t *)((char *)aote + 0xB9)) < 0) {
        goto relock_and_check;
    }
    {
        /* Load VTOCE for local objects */
        uint8_t vol_idx = *((uint8_t *)((char *)aote + 0xB8));
        if (vol_idx <= 0x0F) {
            uint16_t vol_flags = *((uint16_t *)(0xE1E0A0));
            if ((vol_flags & (1 << vol_idx)) != 0) {
                goto bad_volume;                       /* 0x00E022CC -> 0x00E022CE */
            }
        }
    }
    VTOCE_$READ((vtoc_$lookup_req_t *)((char *)aote + 0x9C),
                (vtoce_$result_t *)((char *)aote + 0x0C), status);

    /* Clear per-boot fields if object has them */
    if ((*((uint8_t *)((char *)aote + 0x0F)) & 2) != 0) {
        *((uint32_t *)((char *)aote + 0x50)) = 0;
    }
    goto relock_and_check;

bad_volume:                                            /* 0x00E022CE */
    *status = ast_$validate_uid(uid, 0x30F00);

relock_and_check:
    ML_$LOCK(AST_LOCK_ID);

    /* Re-check volume status after relock */
    if (*((int8_t *)((char *)aote + 0xB9)) >= 0) {
        uint8_t vol_idx = *((uint8_t *)((char *)aote + 0xB8));
        if (vol_idx <= 0x0F) {
            uint16_t vol_flags = *((uint16_t *)(0xE1E0A0));
            if ((vol_flags & (1 << vol_idx)) != 0) {
                *status = ast_$validate_uid(uid, 0x30F00);
            }
        }
    }

    if (*status == status_$ok) {
        /* Success - clear in-transition */
        aote->flags &= ~AOTE_FLAG_IN_TRANS;
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
