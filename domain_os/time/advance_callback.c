/*
 * TIME_$ADVANCE_CALLBACK - Internal callback for TIME_$ADVANCE
 *
 * Called when a TIME_$ADVANCE timer expires. Advances the event count
 * associated with the waiting process.
 *
 * Parameters:
 *   arg - Pointer to callback argument structure
 *         arg[0] points to a structure where offset 0x08 is the EC
 *
 * Original address: 0x00e16434
 *
 * Assembly:
 *   00e16434    link.w A6,-0x4
 *   00e16438    pea (A2)
 *   00e1643a    movea.l (0x8,A6),A0       ; arg
 *   00e1643e    movea.l (A0),A1           ; *arg
 *   00e16440    movea.l (0x8,A1),A2       ; (*arg)->ec at offset 0x08
 *   00e16444    pea (A2)
 *   00e16446    jsr 0x00e20718.l          ; EC_$ADVANCE_WITHOUT_DISPATCH
 *   00e1644c    movea.l (-0x8,A6),A2
 *   00e16450    unlk A6
 *   00e16452    rts
 */

#include "time.h"
#include "ec/ec.h"

/* External reference */
extern void EC_$ADVANCE_WITHOUT_DISPATCH(void *ec);

void TIME_$ADVANCE_CALLBACK(void *arg)
{
    uint32_t **arg_ptr = (uint32_t **)arg;
    uint32_t *inner = *arg_ptr;
    void *ec = (void *)(uintptr_t)inner[2];  /* Offset 0x08 = index 2 */

    EC_$ADVANCE_WITHOUT_DISPATCH(ec);
}
