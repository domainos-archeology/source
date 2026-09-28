/*
 * log_$read_internal - Copy part of the mapped log buffer to a caller
 *
 * Re-emitted from the image (0x00E17778..0x00E177FE, 136 bytes) and
 * verified; the previous body was faithful.
 *
 * Frame (link.w A6,-0x10; A5 = 0xE2B280 set by the LOG_$READ / LOG_$READ2
 * gates): (0x8,A6) buffer -> A2, (0xC,A6) offset word -> D0, (0xE,A6)
 * max_len word -> D1, (0x10,A6) actual_len ptr -> A0.
 *
 *   00e1778c  tst.l (0x14,A5) / bne          ; LOG_$LOGFILE_PTR
 *   00e17792  clr.w (A0) ; exit              ; no log: 0 bytes
 *   00e17796  move.w #0x400,(A0)
 *   00e1779a  cmpi.w #0x400,D1w / bcc        ; max_len < 0x400 (unsigned) ->
 *   00e177a0  moveq #-2 ; and.w D1w ; (A0)   ;   *actual_len = max_len & ~1
 *   00e177a6  D1 = offset, D2 = *actual_len (zero-extended), D3 = 0x400
 *   00e177b4  add.l D1,D2 ; cmp.l D3,D2 / bls ; offset + len > 0x400 ->
 *   00e177ba  cmpi.w #0x400,D0w / bls        ;   offset > 0x400 -> *actual_len = 0
 *   00e177c4  else *actual_len = 0x400 - offset
 *   00e177cc  A1 = LOG_$LOGFILE_PTR + offset ; D0 = *actual_len >> 1 ; -1 ; bmi
 *   00e177e8  dbf: copy that many WORDS to the buffer
 *
 * Original address: 0x00e17778
 */

#include "log/log_internal.h"

void log_$read_internal(void *buffer, uint16_t offset, uint16_t max_len, uint16_t *actual_len)
{
    int16_t *src;
    int16_t *dst;
    int16_t words;               /* D0 after the shift and decrement */
    int16_t i;

    /* 0x00E1778C-0x00E17794 */
    if (LOG_$LOGFILE_PTR == NULL) {
        *actual_len = 0;
        return;
    }

    /* 0x00E17796-0x00E177A4 */
    *actual_len = LOG_BUFFER_SIZE;
    if (max_len < LOG_BUFFER_SIZE) {
        *actual_len = (uint16_t)(max_len & 0xFFFE);
    }

    /* 0x00E177A6-0x00E177CA: 32-bit sum of the two zero-extended words */
    if ((uint32_t)offset + (uint32_t)*actual_len > LOG_BUFFER_SIZE) {
        if (offset > LOG_BUFFER_SIZE) {
            *actual_len = 0;
        } else {
            *actual_len = (uint16_t)(LOG_BUFFER_SIZE - offset);
        }
    }

    /* 0x00E177CC-0x00E177F2 */
    dst = (int16_t *)buffer;
    src = (int16_t *)((uint8_t *)LOG_$LOGFILE_PTR + offset);
    words = (int16_t)((*actual_len >> 1) - 1);
    if (words >= 0) {
        for (i = 0; i <= words; i++) {
            dst[i] = src[i];
        }
    }
}
