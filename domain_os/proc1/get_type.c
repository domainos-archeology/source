/*
 * PROC1_$GET_TYPE - Get process type
 *
 * Parameters:
 *   pid - Process ID
 *
 * Returns:
 *   Process type
 *
 * Original address: 0x00e15324
 */

#include "proc1.h"

uint16_t PROC1_$GET_TYPE(uint16_t pid)
{
    return PROC1_$TYPE[pid];
}
