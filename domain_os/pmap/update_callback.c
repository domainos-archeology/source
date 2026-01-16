/*
 * PMAP_$UPDATE_CALLBACK - Callback to trigger AST update
 *
 * Simple callback function that triggers the AST_$UPDATE routine.
 * Called periodically by the timer system to update the Active
 * Segment Table.
 *
 * Original address: 0x00e143b2
 */

#include "pmap/pmap.h"
#include "ast/ast.h"

void PMAP_$UPDATE_CALLBACK(void)
{
    AST_$UPDATE();
}
