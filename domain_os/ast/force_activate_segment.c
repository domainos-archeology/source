/*
 * ast_$force_activate_segment - Force lookup/create AOTE for an object
 *
 * Looks up or creates an AOTE for the given UID. If the object doesn't
 * exist in the cache, allocates a new AOTE and loads the object info.
 *
 * Parameters:
 *   uid - Object UID to activate
 *   segment - Volume/segment info (high bit = remote, low bits = vol index or network node)
 *   status - Output status
 *   force - Force flag (negative = force activation)
 *
 * Returns: Pointer to AOTE (or NULL on error)
 *
 * Original address: 0x00e020fa
 */

#include "ast/ast_internal.h"

/* External function prototypes */

/* Network info flags */
#if defined(M68K)
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

aote_t *ast_$force_activate_segment(uid_t *uid, uint16_t segment,
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
    *((uint32_t *)((char *)aote + 0x08)) = segment;

    /* Copy UID to AOTE (offset 0x10 and 0x14) */
    *((uint32_t *)((char *)aote + 0x10)) = uid->high;
    *((uint32_t *)((char *)aote + 0x14)) = uid->low;

    /* Initialize UID info area (offset 0x9C) */
    *((uint8_t *)((char *)aote + 0x9C)) = 0;
    *((uint32_t *)((char *)aote + 0xA4)) = uid->high;
    *((uint32_t *)((char *)aote + 0xA8)) = uid->low;

    /* Set remote flag based on segment */
    if ((int16_t)segment < 0) {
        /* Remote object */
        *((uint8_t *)((char *)aote + 0xB9)) = 0x80;  /* Set remote flag */
        *((uint8_t *)((char *)aote + 0xB8)) = 0;     /* Clear vol index */
        *((uint32_t *)((char *)aote + 0xB0)) = segment & 0xFFFFF;  /* Network node */
        NETWORK_$GET_NET(segment, (uint32_t *)(aote + 0xAC), status);
    } else {
        /* Local object */
        *((uint8_t *)((char *)aote + 0xB9)) &= 0x7F;  /* Clear remote flag */
        *((uint8_t *)((char *)aote + 0xB8)) = (uint8_t)(segment & 0x7FFFFFFF);
    }

    /* Insert into hash chain */
    hash_index = hash_index << 2;  /* Convert to byte offset */
    aote->hash_next = AST_AOTH_BASE[hash_index >> 2];
    AST_AOTH_BASE[hash_index >> 2] = aote;

    /* Release AST lock for I/O */
    ML_$UNLOCK(AST_LOCK_ID);

    /* Load object info */
    if ((segment & 0x7FFFFFFF) == 0) {
        /* Root/system object */
        if (force < 0) {
            FUN_00e01bee((char *)aote + 0x9C, status);
        } else {
            FUN_00e01c52((char *)aote + 0x9C, (uint32_t *)((char *)aote + 0x0C),
                        (char *)aote + 0x0C, status);
            if (*status != status_$ok) {
                goto relock_and_check;
            }
            if (*((int8_t *)((char *)aote + 0xB9)) < 0) {
                *((uint32_t *)((char *)aote + 0x08)) = segment;
            }
        }
        if (*status == status_$ok) {
            *((uint32_t *)((char *)aote + 0x08)) = *((uint32_t *)((char *)aote + 0xA0));
        }
    } else {
        /* Non-root object */
        if (*((int8_t *)((char *)aote + 0xB9)) < 0) {
            /* Remote */
            NETWORK_$AST_GET_INFO((char *)aote + 0x9C, NET_INFO_FLAGS,
                                 (char *)aote + 0x0C, status);
        } else {
            /* Local - check volume status */
            uint8_t vol_idx = *((uint8_t *)((char *)aote + 0xB8));
            if (vol_idx <= 0x0F) {
                uint16_t vol_flags = *((uint16_t *)(0xE1E0A0));  /* A5+0x420 */
                if ((vol_flags & (1 << vol_idx)) != 0) {
                    *status = ast_$validate_uid(uid, 0x30F00);
                    goto relock_and_check;
                }
            }
            VTOC_$LOOKUP((vtoc_$lookup_req_t *)((char *)aote + 0x9C), status);
        }
    }

    if (*status == status_$ok && *((int8_t *)((char *)aote + 0xB9)) >= 0) {
        /* Load VTOCE for local objects */
        uint8_t vol_idx = *((uint8_t *)((char *)aote + 0xB8));
        if (vol_idx <= 0x0F) {
            uint16_t vol_flags = *((uint16_t *)(0xE1E0A0));
            if ((vol_flags & (1 << vol_idx)) != 0) {
                *status = ast_$validate_uid(uid, 0x30F00);
                goto relock_and_check;
            }
        }
        VTOCE_$READ((vtoc_$lookup_req_t *)((char *)aote + 0x9C),
                    (vtoce_$result_t *)((char *)aote + 0x0C), status);

        /* Clear per-boot fields if object has them */
        if ((*((uint8_t *)((char *)aote + 0x0F)) & 2) != 0) {
            *((uint32_t *)((char *)aote + 0x50)) = 0;
        }
    }

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
