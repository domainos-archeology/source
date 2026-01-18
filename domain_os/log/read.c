/*
 * LOG_$READ - Read log entries from the beginning
 *
 * Reads log data starting from offset 0. This is a wrapper around
 * the internal log_$read_internal function.
 *
 * Original address: 00e17800
 * Original size: 40 bytes
 *
 * Assembly:
 *   00e17800    link.w A6,0x0
 *   00e17804    pea (A5)
 *   00e17806    lea (0xe2b280).l,A5
 *   00e1780c    move.l (0x10,A6),-(SP)   ; actual_len
 *   00e17810    movea.l (0xc,A6),A0
 *   00e17814    move.w (A0),-(SP)        ; *max_len
 *   00e17816    clr.w -(SP)              ; offset = 0
 *   00e17818    move.l (0x8,A6),-(SP)    ; buffer
 *   00e1781c    bsr.w log_$read_internal
 */

#include "log/log_internal.h"

void LOG_$READ(void *buffer, uint16_t *max_len, uint16_t *actual_len)
{
    log_$read_internal(buffer, 0, *max_len, actual_len);
}
