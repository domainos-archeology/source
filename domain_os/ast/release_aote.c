/*
 * ast_$release_aote - Release/free an AOTE back to the free list
 *
 * Returns an AOTE to the free list after clearing its UID and
 * marking it as free. Updates statistics counters.
 *
 * Original address: 0x00e00f7c
 */

#include "ast/ast_internal.h"

/* External references */

/*
 * Free AOTE list head at A5+0x3EC
 */
#if defined(M68K)
#define AST_$FREE_AOTE_HEAD (*(aote_t **)0xE1E06C)  /* A5+0x3EC */
#define AST_$FREE_AOTES     (*(uint16_t *)0xE1E0EA) /* A5+0x46A */
#else
#define AST_$FREE_AOTE_HEAD ast_$free_aote_head
#define AST_$FREE_AOTES     ast_$free_aotes
#endif

void ast_$release_aote(aote_t *aote)
{
    /* Clear the object UID by copying nil UID */
    /* aote+0x10 = obj_uid.high, aote+0x14 = obj_uid.low */
    *((uint32_t *)((char *)aote + 0x10)) = UID_$NIL.high;
    *((uint32_t *)((char *)aote + 0x14)) = UID_$NIL.low;

    /* Clear the remote flag (bit 7 at offset 0xB9) */
    *((uint8_t *)((char *)aote + 0xB9)) &= 0x7F;

    /* Clear reference count (offset 0xB8) */
    *((uint8_t *)((char *)aote + 0xB8)) = 0;

    /* Link to free list */
    aote->hash_next = AST_$FREE_AOTE_HEAD;
    AST_$FREE_AOTE_HEAD = aote;

    /* Set the in-transition flag (bit 7 at offset 0xBF) */
    aote->flags |= AOTE_FLAG_IN_TRANS;

    /* Signal that an AST operation completed */
    EC_$ADVANCE(&AST_$AST_IN_TRANS_EC);

    /* Increment free count */
    AST_$FREE_AOTES++;
}
