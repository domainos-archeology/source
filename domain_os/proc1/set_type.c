/*
 * PROC1_$SET_TYPE - Set process type
 *
 * Parameters:
 *   pid - Process ID
 *   type - New process type
 *
 * Original address: 0x00e152e4
 */

#include "proc1.h"

void PROC1_$SET_TYPE(uint16_t pid, uint16_t type)
{
    PROC1_$TYPE[pid] = type;
}
