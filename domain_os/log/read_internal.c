/*
 * log_$read_internal - Internal log read implementation
 *
 * Reads log data from the mapped buffer with bounds checking.
 * Copies data word-by-word from the log buffer to the destination.
 *
 * Original address: 00e17778
 * Original size: 136 bytes
 *
 * Assembly (key parts):
 *   00e1778c    tst.l (0x14,A5)         ; check LOG_$LOGFILE_PTR
 *   00e17790    bne.b continue
 *   00e17792    clr.w (A0)              ; *actual_len = 0
 *   00e17796    move.w #0x400,(A0)      ; *actual_len = 1024 max
 *   00e177e8    move.w (-0x2,A0),(-0x2,A1)  ; copy words
 */

#include "log/log_internal.h"

void log_$read_internal(void *buffer, uint16_t offset, uint16_t max_len, uint16_t *actual_len)
{
    int16_t *src;
    int16_t *dst;
    int16_t words;
    int16_t i;

    /* If log not initialized, return 0 bytes */
    if (LOG_$LOGFILE_PTR == NULL) {
        *actual_len = 0;
        return;
    }

    /* Clamp max_len to buffer size */
    *actual_len = LOG_BUFFER_SIZE;
    if (max_len < LOG_BUFFER_SIZE) {
        *actual_len = max_len & 0xFFFE;  /* Round down to word boundary */
    }

    /* Check bounds: offset + length must not exceed buffer */
    if ((uint32_t)offset + (uint32_t)*actual_len > LOG_BUFFER_SIZE) {
        if (offset > LOG_BUFFER_SIZE) {
            *actual_len = 0;
        } else {
            *actual_len = LOG_BUFFER_SIZE - offset;
        }
    }

    /* Copy data word-by-word */
    dst = (int16_t *)buffer;
    src = (int16_t *)((uint8_t *)LOG_$LOGFILE_PTR + offset);

    words = (*actual_len >> 1) - 1;
    if (words >= 0) {
        for (i = 0; i <= words; i++) {
            dst[i] = src[i];
        }
    }
}
