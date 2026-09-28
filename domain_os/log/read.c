/*
 * log/read.c - LOG_$READ (0x00E17800, 40 bytes)
 *
 * Gate for log_$read_internal: reads from offset 0 with the word count the
 * caller passes BY ADDRESS.
 *
 *   00e1780c    move.l (0x10,A6),-(SP)   ; actual_len (argument 3)
 *   00e17810    movea.l (0xc,A6),A0
 *   00e17814    move.w (A0),-(SP)        ; *max_len   (argument 2, by address)
 *   00e17816    clr.w -(SP)              ; offset 0
 *   00e17818    move.l (0x8,A6),-(SP)    ; buffer     (argument 1)
 *   00e1781c    bsr.w log_$read_internal
 *
 * A5 is saved and set to 0xE2B280 around the call (`pea (A5)` / `lea`); the
 * body's A5 accesses are what need it.
 */

#include "log/log_internal.h"

void LOG_$READ(void *buffer, uint16_t *max_len, uint16_t *actual_len)
{
    log_$read_internal(buffer, 0, *max_len, actual_len);
}
