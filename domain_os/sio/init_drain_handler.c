/*
 * SIO_$INIT_DRAIN_HANDLER - Fill in a three-longword drain-handler record
 *
 * record[0] = TTY_$I_OUTPUT_BUFFER_DRAINED, record[1] = *context_ptr,
 * record[2] = *data_ptr.  The dtte argument is never read.  Called by
 * SIO_$INIT (0x00E32D02) for the console port.
 *
 * Original address: 0x00E32BB8, 40 bytes (SAU2 map: OS_TERM segment)
 *
 *   00e32bb8    link.w A6,-0x4
 *   00e32bbc    pea (A2)
 *   00e32bbe    movea.l (0x8,A6),A0            ; arg 1 handler record
 *   00e32bc2    movea.l (0x10,A6),A1           ; arg 3 data_ptr
 *   00e32bc6    move.l (A1),(0x8,A0)           ; record[2] = *data_ptr
 *   00e32bca    move.l #0xe1b394,(A0)          ; record[0] = TTY_$I_OUTPUT_BUFFER_DRAINED
 *   00e32bd0    movea.l (0x14,A6),A2           ; arg 4 context_ptr
 *   00e32bd4    move.l (A2),(0x4,A0)           ; record[1] = *context_ptr
 *   00e32bd8    movea.l (-0x8,A6),A2
 *   00e32bdc    unlk A6
 *   00e32bde    rts
 */

#include "sio/sio_internal.h"

void SIO_$INIT_DRAIN_HANDLER(m68k_ptr_t *handler, void *dtte,
                              m68k_ptr_t *data_ptr, m68k_ptr_t *context_ptr)
{
    (void)dtte;     /* (0xC,A6) is never read */

    /* 0x00E32BC6-0x00E32BD4 */
    handler[2] = *data_ptr;
    handler[0] = ARCH_PTR_TO_VA(TTY_$I_OUTPUT_BUFFER_DRAINED);
    handler[1] = *context_ptr;
}
