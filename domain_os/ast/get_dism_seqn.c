/*
 * AST_$GET_DISM_SEQN - Get dismount sequence number
 *
 * Returns the current dismount sequence number, which is
 * incremented each time a volume is dismounted.
 *
 * Original address: 0x00e01388
 */

#include "ast.h"

uint32_t AST_$GET_DISM_SEQN(void)
{
    return AST_$DISM_SEQN;
}
