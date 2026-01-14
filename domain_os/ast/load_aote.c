/*
 * AST_$LOAD_AOTE - Load an AOTE with object attributes
 *
 * Either updates an existing AOTE or creates a new one for the
 * specified object UID, populating it with the provided attributes.
 *
 * Original address: 0x00e0238c
 */

#include "ast/ast_internal.h"

void AST_$LOAD_AOTE(uint32_t *attrs, uint32_t *obj_info)
{
    aote_t *aote;
    aote_t *existing;
    uint32_t saved_dtv;
    uint16_t saved_dtv_frac;
    int16_t hash_idx;
    int32_t old_seqn;
    uid_t *uid;
    int i;

    uid = (uid_t *)(obj_info + 2);  /* UID at offset 8 */

    ML_$LOCK(AST_LOCK_ID);

    /* Look up existing AOTE */
    existing = FUN_00e0209e(uid);

    if (existing != NULL) {
        /* Update existing AOTE - preserve date/time */
        saved_dtv = *(uint32_t *)((char *)existing + 0x38);
        saved_dtv_frac = *(uint16_t *)((char *)existing + 0x3C);

        /* Copy attributes (144 bytes = 36 words) */
        uint32_t *src = attrs;
        uint32_t *dst = (uint32_t *)((char *)existing + 0x0C);
        for (i = 0x23; i >= 0; i--) {
            *dst++ = *src++;
        }

        /* Restore date/time */
        *(uint32_t *)((char *)existing + 0x38) = saved_dtv;
        *(uint16_t *)((char *)existing + 0x3C) = saved_dtv_frac;

        /* Clear touched flag */
        existing->flags &= ~AOTE_FLAG_TOUCHED;

        goto done;
    }

    /* Need to create new AOTE */
    old_seqn = AST_$AOTE_SEQN;

    /* Allocate new AOTE */
    aote = FUN_00e01d66();

    /* Compute hash index */
    hash_idx = UID_$HASH(uid, (void *)0xE01BEC);

    /* Check if another AOTE was created for same UID while we were allocating */
    if (old_seqn != AST_$AOTE_SEQN) {
        /* Search hash chain for existing entry */
        for (existing = (aote_t *)((uint32_t *)&AOTH)[hash_idx];
             existing != NULL;
             existing = existing->hash_next) {
            if (*(uint32_t *)((char *)existing + 0x10) == obj_info[2] &&
                *(uint32_t *)((char *)existing + 0x14) == obj_info[3]) {
                /* Found existing - free the one we allocated */
                FUN_00e00f7c(aote);
                goto done;
            }
        }
    }

    /* Check if volume is being dismounted */
    uint8_t vol_index = *((uint8_t *)(obj_info + 7));
    if (vol_index < 0x10 && (DAT_00e1e0a0 & (1 << vol_index)) != 0) {
        FUN_00e00f7c(aote);
        goto done;
    }

    /* Initialize new AOTE */
    AST_$AOTE_SEQN++;
    aote->flags &= ~AOTE_FLAG_IN_TRANS;
    aote->flags &= ~AOTE_FLAG_BUSY;
    aote->flags &= ~AOTE_FLAG_DIRTY;
    aote->ref_count = 0;
    aote->status_flags = 0;
    aote->hash_next = NULL;
    aote->aste_list = NULL;

    /* Copy object info (UID and related) - 32 bytes */
    uint32_t *src = obj_info;
    uint32_t *dst = (uint32_t *)((char *)aote + 0x9C);
    for (i = 7; i >= 0; i--) {
        *dst++ = *src++;
    }

    /* Check if remote object */
    uint32_t node_id = obj_info[5];
    uint8_t *remote_flag = (uint8_t *)((char *)aote + 0xB9);
    *remote_flag &= 0x7F;
    if (node_id != NODE_$ME) {
        *remote_flag |= 0x80;
    }

    if (*(int8_t *)remote_flag < 0) {
        /* Remote object - set up network info */
        status_$t status;
        NETWORK_$INSTALL_NET(obj_info[4], (void *)((char *)aote + 0x08), &status);
        if (status != status_$ok) {
            FUN_00e00f7c(aote);
            goto done;
        }
        /* Set up volume UID with network info */
        aote->vol_uid = (aote->vol_uid & 0xFFF00000) | node_id;
        *(uint8_t *)((char *)aote + 0x08) |= 0x80;
    } else {
        /* Local object - copy volume UID */
        aote->vol_uid = obj_info[1];
    }

    /* Copy attributes */
    src = attrs;
    dst = (uint32_t *)((char *)aote + 0x0C);
    for (i = 0x23; i >= 0; i--) {
        *dst++ = *src++;
    }

    /* Insert into hash chain */
    aote->hash_next = (aote_t *)((uint32_t *)&AOTH)[hash_idx];
    ((uint32_t *)&AOTH)[hash_idx] = (uint32_t)aote;

done:
    ML_$UNLOCK(AST_LOCK_ID);
}
