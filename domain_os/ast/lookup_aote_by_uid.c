/*
 * ast_$lookup_aote_by_uid - Find an object's AOTE on the hash chain
 *
 * Hashes the UID, walks the chain and returns the entry whose UID
 * (aote+0x10) matches.  A matching entry that is in transition (flags bit
 * 7) is waited for, after which the walk restarts from the chain head.
 * Returns NULL when no entry matches.
 *
 * Original address: 0x00E0209E (92 bytes), A5 = 0xE1DC80: the AOTH is
 * `(0x0,A5,D3w)` with D3w = hash * 4.
 */

#include "ast/ast_internal.h"

/* The AOTE hash table, `AOTH` in the SAU2 map, is AST_$DATA.aoth (ast/ast.h). */

/*
 * UID_$HASH's table-size word: `pea (-0x4c6,PC)` at 0x00E020B0 ->
 * 0x00E01BEC, image bytes 00 FB (251 buckets).  The same cell is used by
 * ast_$force_activate_segment, AST_$LOAD_AOTE, ast_$process_aote and
 * AST_$LOOKUP_WITH_HINTS.
 * TODO(source-s1h0): the image holds this literal ONCE; the four private copies
 * should become one definition in ast_data.c once the asta2 batch lands.
 */
static const uint16_t ast_$aoth_hash_size_00e01bec = 0x00FB;

aote_t *ast_$lookup_aote_by_uid(uid_t *uid)
{
    uint16_t hash_index;        /* D2w */
    aote_t *aote;               /* A0 */

    /* 0x00E020B0..0x00E020C2 */
    hash_index = (uint16_t)UID_$HASH(uid,
                                     (uint16_t *)&ast_$aoth_hash_size_00e01bec);

    /* 0x00E020E0: the chain head */
    aote = AST_$DATA.aoth[hash_index];
    while (aote != NULL) {
        /* 0x00E020C6..0x00E020D4: two cmpm.l over aote+0x10 */
        if (aote->uid.high == uid->high && aote->uid.low == uid->low) {
            /* 0x00E020D6: tst.b (0xbf,A0) */
            if ((int8_t)aote->flags >= 0) {
                return aote;                            /* 0x00E020F0 */
            }
            /* 0x00E020DC..0x00E020E0: wait, then restart from the head */
            AST_$WAIT_FOR_AST_INTRANS();
            aote = AST_$DATA.aoth[hash_index];
            continue;
        }
        /* 0x00E020E6 */
        aote = aote->hash_next;
    }

    /* 0x00E020EE */
    return NULL;
}
