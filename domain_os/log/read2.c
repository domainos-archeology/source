/*
 * LOG_$READ2 - Read the system log from an offset
 *
 * Re-emitted from the image (0x00E17828..0x00E1784E, 40 bytes) and
 * verified; the previous body was faithful.  A5 is set to the LOG module
 * block (0xE2B280) for the callee, which reads (0x14,A5).
 *
 *   00e17834  move.l (0x10,A6),-(SP)   ; actual_len  (arg 4)
 *   00e17838  move.w (0xe,A6),-(SP)    ; max_len     (arg 3)
 *   00e1783c  move.w (0xc,A6),-(SP)    ; offset      (arg 2)
 *   00e17840  move.l (0x8,A6),-(SP)    ; buffer      (arg 1)
 *   00e17844  bsr.w log_$read_internal (0x00E17778)
 *
 * The 12 bytes of arguments are left for `unlk`.  Callers: 0x00E64F54 and
 * 0x00E65D6C (ASKNODE).
 *
 * Original address: 0x00e17828
 */

#include "log/log_internal.h"

void LOG_$READ2(void *buffer, uint16_t offset, uint16_t max_len, uint16_t *actual_len)
{
    log_$read_internal(buffer, offset, max_len, actual_len);
}
