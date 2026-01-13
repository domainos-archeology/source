/*
 * AST_$ACTIVATE_AOTE_CANNED - Activate an AOTE with pre-packaged attributes
 *
 * Creates and activates an AOTE entry using pre-computed attributes
 * and UID information. Used during system initialization for known objects.
 *
 * Parameters:
 *   attrs - Pointer to attribute block (144 bytes)
 *   obj_info - Pointer to object info (32 bytes including UID)
 *
 * Original address: 0x00e2f0c2
 */

#include "ast.h"

/* Internal function prototypes */
extern aote_t* FUN_00e01d66(void);  /* Allocate AOTE */
extern uint16_t UID_$HASH(uid_t *uid, void *table);

/* External data */
extern status_$t status_$_00e2f1d0;  /* Duplicate AOTE error */

void AST_$ACTIVATE_AOTE_CANNED(uint32_t *attrs, uint32_t *obj_info)
{
    aote_t *aote;
    aote_t *hash_chain;
    uint16_t hash;
    int i;

    ML_$LOCK(AST_LOCK_ID);

    /* Allocate new AOTE */
    aote = FUN_00e01d66();

    /* Clear flags */
    *(uint8_t *)((char *)aote + 0xBF) &= ~AOTE_FLAG_IN_TRANS;
    *(uint8_t *)((char *)aote + 0xBF) &= ~AOTE_FLAG_BUSY;
    *(uint8_t *)((char *)aote + 0xBF) &= ~AOTE_FLAG_DIRTY;
    *(uint8_t *)((char *)aote + 0xBF) &= ~AOTE_FLAG_TOUCHED;

    /* Initialize fields */
    *(uint8_t *)((char *)aote + 0xBE) = 1;  /* ref_count = 1 */
    aote->status_flags = 0;
    aote->hash_next = NULL;
    aote->aste_list = NULL;

    /* Handle remote vs local object */
    if (*(int8_t *)((char *)obj_info + 0x1D) < 0) {
        /* Remote object */
        *(uint8_t *)((char *)aote + 0x08) |= 0x80;
        *(uint16_t *)((char *)aote + 0x08) &= 0xFC0F;
        aote->vol_uid = aote->vol_uid & 0xFFF00000;
        aote->vol_uid |= obj_info[5];
    } else {
        /* Local object */
        aote->vol_uid = obj_info[1];
    }

    /* Copy attributes (144 bytes = 36 uint32_t) */
    uint32_t *attr_dest = (uint32_t *)((char *)aote + 0x0C);
    for (i = 0; i < 36; i++) {
        attr_dest[i] = attrs[i];
    }

    /* Copy object info/UID (32 bytes = 8 uint32_t) */
    uint32_t *uid_dest = (uint32_t *)((char *)aote + 0x9C);
    for (i = 0; i < 8; i++) {
        uid_dest[i] = obj_info[i];
    }

    /* Calculate hash and insert into hash table */
    hash = UID_$HASH((uid_t *)((char *)aote + 0x10), NULL);
    hash <<= 2;

    /* Check for duplicates */
    hash_chain = *(aote_t **)(AST_GLOBALS_BASE + hash);
    while (hash_chain != NULL) {
        /* Compare UIDs */
        if (hash_chain->obj_uid.high == aote->obj_uid.high &&
            hash_chain->obj_uid.low == aote->obj_uid.low) {
            CRASH_SYSTEM((const char *)&status_$_00e2f1d0);
        }
        hash_chain = hash_chain->hash_next;
    }

    /* Insert at head of hash chain */
    aote->hash_next = *(aote_t **)(AST_GLOBALS_BASE + hash);
    *(aote_t **)(AST_GLOBALS_BASE + hash) = aote;

    ML_$UNLOCK(AST_LOCK_ID);
}
