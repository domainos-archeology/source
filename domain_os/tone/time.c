/*
 * TONE_$TIME - Play a tone for a specified duration
 *
 * This function plays a tone for the duration specified by the caller.
 * It acquires lock 0x0E to prevent concurrent tone operations, then
 * enables the tone, waits for the specified duration, and disables it.
 *
 * Original address: 0x00e172fc
 *
 * Assembly analysis:
 *   - Copies 6-byte clock_t from parameter to local stack
 *   - Acquires PROC1 lock 0x0E
 *   - Enables tone via TONE_$ENABLE(0xFF)
 *   - Waits via TIME_$WAIT(delay_type=0, duration)
 *   - Disables tone via TONE_$ENABLE(0x00)
 *   - Releases PROC1 lock 0x0E
 */

#include "tone/tone_internal.h"

/*
 * Static data constants (originally at 0xE1735A-0xE1735F)
 *
 * The original code used PC-relative addressing to access these
 * constants embedded after the function code.
 */
static uint8_t tone_enable_byte = TONE_ENABLE_VALUE;    /* 0xE1735A */
static uint8_t tone_disable_byte = TONE_DISABLE_VALUE;  /* 0xE1735C */
static uint16_t tone_wait_type = TONE_WAIT_RELATIVE;    /* 0xE1735E */

/*
 * TONE_$TIME - Play tone for specified duration
 *
 * Parameters:
 *   duration - Pointer to clock_t containing the tone duration
 *              (first 4 bytes = high, next 2 bytes = low)
 *
 * The lock prevents concurrent tones from overlapping and ensures
 * only one tone can play at a time system-wide.
 */
void TONE_$TIME(clock_t *duration)
{
    clock_t local_duration;
    status_$t status;

    /*
     * Copy duration to local storage.
     * Original assembly:
     *   movea.l (0x8,A6),A0     ; A0 = duration pointer
     *   move.l (A0),(-0x8,A6)   ; copy high 32 bits
     *   move.w (0x4,A0),(-0x4,A6) ; copy low 16 bits
     */
    local_duration.high = duration->high;
    local_duration.low = duration->low;

    /* Acquire tone lock to prevent concurrent tone generation */
    PROC1_$SET_LOCK(TONE_LOCK_ID);

    /* Enable the tone output */
    TONE_$ENABLE(&tone_enable_byte);

    /* Wait for the specified duration (relative wait) */
    TIME_$WAIT(&tone_wait_type, &local_duration, &status);

    /* Disable the tone output */
    TONE_$ENABLE(&tone_disable_byte);

    /* Release the tone lock */
    PROC1_$CLR_LOCK(TONE_LOCK_ID);
}
