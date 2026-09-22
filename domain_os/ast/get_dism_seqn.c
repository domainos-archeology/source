/*
 * AST_$GET_DISM_SEQN - Get dismount sequence number
 *
 * Returns the current dismount sequence number, which is
 * incremented each time a volume is dismounted.
 *
 * Original address: 0x00E01388 (24 bytes): `pea (A5)` / `lea (0xe1dc80).l,A5`
 * / `move.l (0x404,A5),D0` - AST_$DISM_SEQN at 0xE1E084 - and restore A5.
 */

#include "ast/ast_internal.h"

uint32_t AST_$GET_DISM_SEQN(void)
{
    return AST_$DISM_SEQN;
}
