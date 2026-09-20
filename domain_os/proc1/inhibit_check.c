/*
 * PROC1_$INHIBIT_CHECK - Is the process inside an inhibit region?
 * Original address: 0x00e20ef0 (12 bytes)
 *
 * 0x00E20EF0  movea.l (0x4,SP),A1            A1 = pcb (argument 1)
 * 0x00E20EF4  tst.w (0x5a,A1)                pcb->nesting_depth
 * 0x00E20EF8  sne D0b                        0xFF when non-zero, else 0
 * 0x00E20EFA  rts
 *
 * Callers: PROC1_$TRY_TO_SUSPEND (0x00E1472E) and FIM (0x00E0A5A8); both
 * test the byte result with tst.b / bmi, so it is a Domain boolean.
 */

#include "proc1/proc1_internal.h"

int8_t PROC1_$INHIBIT_CHECK(proc1_t *pcb)
{
    /* 0x00E20EF4 / 0x00E20EF8 */
    return (pcb->nesting_depth != 0) ? -1 : 0;
}
