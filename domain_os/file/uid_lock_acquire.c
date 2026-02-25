/*
 * FILE_$UID_LOCK_ACQUIRE - Acquire per-UID hash-bucket lock
 *
 * Original address: 0x00E5D0A8
 *
 * This function implements a fine-grained per-UID lock used by the
 * file delete path. It hashes the UID to one of 17 buckets and
 * busy-waits (with EC_$WAITN) until the bucket is free. The caller
 * must hold ML lock 5 when calling this function.
 *
 * The lock is stored as a byte in a 17-entry array (one per hash
 * bucket). A zero byte means the slot is free; a non-zero byte
 * holds the low byte of the owning process's PID.
 *
 * When the slot is busy, the function:
 *   1. Reads FILE_$UID_LOCK_EC.value + 1 as a wait target
 *   2. Releases ML lock 5 (to allow the holder to finish)
 *   3. Waits on FILE_$UID_LOCK_EC via EC_$WAITN
 *   4. Re-acquires ML lock 5
 *   5. Retries the test
 *
 * This pattern is a classic "condition variable" wait in Domain/OS.
 *
 * Assembly at 0x00E5D0A8:
 *   link.w A6,-0x14
 *   movem.l { A3 A2 D2},-(SP)
 *   movea.l (0x8,A6),A0       ; A0 = uid
 *   move.l (A0),D1            ; D1 = uid->high
 *   move.l (0x4,A0),D2        ; D2 = uid->low
 *   eor.l D2,D1               ; D1 = high ^ low
 *   ... (fold 16-bit halves, % 17) ...
 *   tst.b (0x0,A5,D2w*0x1)   ; Test lock holder byte
 *   bne.b <wait>
 *   move.b (0x00e20609).l,(0x0,A5,D2w*0x1) ; Store PID low byte
 *   ...
 */

#include "file/file_internal.h"
#include "ml/ml.h"
#include "ec/ec.h"

/* File lock ID */
#define FILE_LOCK_ID    5

/* Number of hash buckets for UID lock holders */
#define FILE_UID_LOCK_BUCKETS  17

/*
 * FILE_$UID_LOCK_ACQUIRE
 *
 * Acquires a per-UID hash-bucket lock. The caller must hold ML lock 5.
 * The lock is released by FILE_$UID_LOCK_RELEASE.
 *
 * Parameters:
 *   uid - UID to lock (used for hash computation only)
 *
 * The hash is computed as:
 *   temp = uid->high ^ uid->low
 *   hash = ((temp & 0xFFFF) ^ ((temp >> 16) & 0xFFFF)) % 17
 *
 * The lock byte is stored in the per-process data area at A5-relative
 * offset. For the C implementation, this is accessed via the global
 * FILE_$UID_LOCK_HOLDERS array.
 */
void FILE_$UID_LOCK_ACQUIRE(uid_t *uid)
{
    uint32_t temp;
    uint16_t hash;
    int32_t wait_val;
    ec_$eventcount_t *ec_ptrs[1];

    /* Hash the UID to a bucket index (0-16) */
    temp = uid->high ^ uid->low;
    hash = ((uint16_t)(temp & 0xFFFF) ^ (uint16_t)((temp >> 16) & 0xFFFF))
           % FILE_UID_LOCK_BUCKETS;

    /* Spin-wait until the bucket is free */
    while (FILE_$UID_LOCK_HOLDERS[hash] != 0) {
        /* Record the EC value we need to wait past */
        wait_val = FILE_$UID_LOCK_EC.value + 1;

        /* Release ML lock 5 so the holder can make progress */
        ML_$UNLOCK(FILE_LOCK_ID);

        /* Wait for the eventcount to advance */
        ec_ptrs[0] = &FILE_$UID_LOCK_EC;
        EC_$WAITN(ec_ptrs, &wait_val, 1);

        /* Re-acquire ML lock 5 and retry */
        ML_$LOCK(FILE_LOCK_ID);
    }

    /*
     * Slot is free - claim it by storing the low byte of the current PID.
     * On m68k, PROC1_$CURRENT is a uint16_t at 0xE20608, and the code
     * reads from 0xE20609 (the low byte, big-endian). We use a mask to
     * extract the low byte portably.
     */
    FILE_$UID_LOCK_HOLDERS[hash] = (uint8_t)(PROC1_$CURRENT & 0xFF);
}
