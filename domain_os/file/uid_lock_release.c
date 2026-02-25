/*
 * FILE_$UID_LOCK_RELEASE - Release per-UID hash-bucket lock
 *
 * Original address: 0x00E5D134
 *
 * This function releases a per-UID hash-bucket lock previously
 * acquired by FILE_$UID_LOCK_ACQUIRE. It clears the lock holder
 * byte and advances the FILE_$UID_LOCK_EC eventcount to wake
 * any waiters.
 *
 * The caller must hold ML lock 5 when calling this function.
 *
 * Assembly at 0x00E5D134:
 *   link.w A6,-0xc
 *   move.l D2,-(SP)
 *   movea.l (0x8,A6),A0        ; A0 = uid
 *   move.l (A0),D1             ; D1 = uid->high
 *   move.l (0x4,A0),D0         ; D0 = uid->low
 *   eor.l D0,D1                ; D1 = high ^ low
 *   ... (fold 16-bit halves, % 17) ...
 *   clr.b (0x0,A5,D0w*0x1)    ; Clear lock holder byte
 *   move.l #0xe2c028,-(SP)     ; Push &FILE_$UID_LOCK_EC
 *   jsr 0x00e206ee.l           ; EC_$ADVANCE(&FILE_$UID_LOCK_EC)
 *   ...
 */

#include "file/file_internal.h"
#include "ec/ec.h"

/* Number of hash buckets for UID lock holders (must match acquire) */
#define FILE_UID_LOCK_BUCKETS  17

/*
 * FILE_$UID_LOCK_RELEASE
 *
 * Releases a per-UID hash-bucket lock and wakes any waiters.
 * The caller must hold ML lock 5.
 *
 * Parameters:
 *   uid - UID whose lock to release (used for hash computation only;
 *         must match the UID passed to FILE_$UID_LOCK_ACQUIRE)
 */
void FILE_$UID_LOCK_RELEASE(uid_t *uid)
{
    uint32_t temp;
    uint16_t hash;

    /* Hash the UID to the same bucket index as acquire */
    temp = uid->high ^ uid->low;
    hash = ((uint16_t)(temp & 0xFFFF) ^ (uint16_t)((temp >> 16) & 0xFFFF))
           % FILE_UID_LOCK_BUCKETS;

    /* Clear the lock holder byte */
    FILE_$UID_LOCK_HOLDERS[hash] = 0;

    /* Advance the eventcount to wake any waiters */
    EC_$ADVANCE(&FILE_$UID_LOCK_EC);
}
