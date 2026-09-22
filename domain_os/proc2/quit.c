/*
 * PROC2_$QUIT - Send SIGQUIT to a process
 *
 * Re-emitted from the image (0x00E3F130..0x00E3F156, 40 bytes).
 *
 *   00e3f13c  move.l (0xc,A6),-(SP)     ; status_ret            (arg 4)
 *   00e3f140  pea (0x1a,PC)             ; &0x00E3F15C = param   (arg 3)
 *   00e3f144  pea (0x12,PC)             ; &0x00E3F158 = signal  (arg 2)
 *   00e3f148  move.l (0x8,A6),-(SP)     ; proc_uid              (arg 1)
 *   00e3f14c  bsr.w PROC2_$SIGNAL
 *
 * Only reference: the SVC table entry at 0x00E7B4D6.
 *
 * Original address: 0x00e3f130
 */

#include "proc2/proc2_internal.h"

/*
 * Constant cells in the code region (bytes read from the image):
 *   0x00E3F158  00 03        word 3 = SIGQUIT
 *   0x00E3F15C  00 12 00 10  longword 0x00120010, the signal parameter
 *               (the "20 48" at 0x00E3F15A is the padding between them)
 */
static const int16_t  proc2_quit_signal_00e3f158 = SIGQUIT;
static const uint32_t proc2_quit_param_00e3f15c  = 0x00120010u;

void PROC2_$QUIT(uid_t *proc_uid, status_$t *status_ret)
{
    PROC2_$SIGNAL(proc_uid, (int16_t *)&proc2_quit_signal_00e3f158,
                  (uint32_t *)&proc2_quit_param_00e3f15c, status_ret);
}
