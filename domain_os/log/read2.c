/*
 * LOG_$READ2 - Read log entries with offset
 *
 * Reads log data starting from a specified offset. This is a wrapper
 * around the internal log_$read_internal function.
 *
 * Original address: 00e17828
 * Original size: 40 bytes
 *
 * Assembly:
 *   00e17828    link.w A6,0x0
 *   00e1782c    pea (A5)
 *   00e1782e    lea (0xe2b280).l,A5
 *   00e17834    move.l (0x10,A6),-(SP)   ; actual_len
 *   00e17838    move.w (0xe,A6),-(SP)    ; max_len
 *   00e1783c    move.w (0xc,A6),-(SP)    ; offset
 *   00e17840    move.l (0x8,A6),-(SP)    ; buffer
 *   00e17844    bsr.w log_$read_internal
 */

#include "log/log_internal.h"

void LOG_$READ2(void *buffer, uint16_t offset, uint16_t max_len, uint16_t *actual_len)
{
    log_$read_internal(buffer, offset, max_len, actual_len);
}
