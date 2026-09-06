/*
 * disk_$get_qblks_internal - Internal queue block allocation
 *
 * Allocates queue blocks from the disk module's free pool.
 * Uses ML_$EXCLUSION for synchronization and EC_$WAIT for
 * blocking when insufficient blocks are available.
 *
 * Two allocation modes:
 *   - Read mode (mode >= 0): Enqueues request in circular buffer,
 *     waits for rtn_qblks_internal to fulfill it. Will not allocate
 *     if there are pending requests (ensures FIFO ordering).
 *   - Write mode (mode < 0): Uses a reserve block if available,
 *     or waits on ec+1. Loops until blocks are available.
 *     Writes have priority over reads.
 *
 * When blocks are available, walks the free list and initializes each:
 *   - Clears status, flags, and reserved field
 *   - Sets owner to current process ID
 *   - Links blocks through forward pointer (offset 0x00)
 *   - Terminates list with NULL pointers on last block
 *
 * Parameters:
 *   count     - Number of queue blocks to allocate
 *   mode      - Negative for write mode, non-negative for read
 *   first_out - Output: pointer to first allocated block
 *   last_out  - Output: pointer to last allocated block
 *
 * Original address: 0x00E3BE8A
 * Size: 362 bytes
 */

#include "disk/disk_internal.h"

void disk_$get_qblks_internal(int16_t count, int8_t mode, void *first_out, void *last_out)
{
    uint8_t *data = DISK_$DATA;

    ML_$EXCLUSION_START((ml_$exclusion_t *)(data + DMOD_EXCLUSION));

    do {
        /* Inner loop: try to satisfy the request or grow the pool */
        for (;;) {
            int16_t avail = *(int16_t *)(data + DMOD_AVAIL_COUNT);
            int16_t pending = *(int16_t *)(data + DMOD_PENDING_COUNT);

            /* Can allocate if enough blocks AND either:
             *   - Write mode (mode < 0): always OK (writes have priority)
             *   - Read mode (mode >= 0): only if no pending requests (FIFO) */
            if (count <= avail && (mode < 0 || pending == 0)) {
                *(int16_t *)(data + DMOD_AVAIL_COUNT) = avail - count;
                goto allocate;
            }

            /* Check if we can try to grow the pool:
             * Only possible if not disabled AND no pending requests.
             * Assembly uses: ~disabled & seq(pending==0), tests bit 7.
             * In practice disabled is always 0x00 or 0xFF (clr.b/st). */
            if (*(uint8_t *)(data + DMOD_ALLOC_DISABLED) == 0 && pending == 0) {
                /* Don't grow for process type 5 (helper/idle processes) */
                if (PROC1_$TYPE[PROC1_$CURRENT] == 5) {
                    break;
                }
                disk_$grow_qblk_pool(count);
                continue;  /* Re-check availability */
            }
            break;  /* Can't grow - must wait */
        }

        /* Insufficient blocks available - must wait or use reserve */
        int32_t wait_val;

        if (mode < 0) {
            /* Write mode: check for reserve block */
            if ((int8_t)*(uint8_t *)(data + DMOD_RESERVE_AVAIL) < 0) {
                /* Reserve available - swap it into the free list */
                *(void **)(data + DMOD_FREE_HEAD) = *(void **)(data + DMOD_RESERVE_BLOCK);
                *(uint8_t *)(data + DMOD_RESERVE_AVAIL) = 0;
                goto allocate;
            }
            /* Wait for eventcount value + 1 */
            wait_val = ((ec_$eventcount_t *)data)->value + 1;
        } else {
            /* Read mode: enqueue this request in the circular buffer */
            int16_t write_idx = *(int16_t *)(data + DMOD_REQ_WRITE_IDX);
            *(int16_t *)(data + DMOD_REQ_QUEUE + write_idx * 2) = count;
            if (write_idx == DMOD_REQ_QUEUE_SIZE) {
                *(int16_t *)(data + DMOD_REQ_WRITE_IDX) = 1;  /* Wrap to 1 */
            } else {
                *(int16_t *)(data + DMOD_REQ_WRITE_IDX) = write_idx + 1;
            }
            int16_t new_pending = *(int16_t *)(data + DMOD_PENDING_COUNT) + 1;
            *(int16_t *)(data + DMOD_PENDING_COUNT) = new_pending;
            /* Wait for eventcount value + pending count */
            wait_val = ((ec_$eventcount_t *)data)->value + (int32_t)new_pending;
        }

        /* Release lock, wait on eventcount, re-acquire lock */
        ML_$EXCLUSION_STOP((ml_$exclusion_t *)(data + DMOD_EXCLUSION));

        /* 0xE3BF50-0xE3BF5E: both 3-element arrays go on the stack by
         * value.  ecs = { &DISK_$DATA[0], NULL, NULL } (A2 was loaded with
         * 0 at 0xE3BEA6), vals = { wait_val, 0, 0 }. */
        EC_$WAIT((ec_$wait_ecs_t){ { (ec_$eventcount_t *)data, NULL, NULL } },
                 (ec_$wait_vals_t){ { wait_val, 0, 0 } });

        ML_$EXCLUSION_START((ml_$exclusion_t *)(data + DMOD_EXCLUSION));
    } while (mode < 0);  /* Write mode loops; read mode falls through */

allocate:
    /* Record the first block (current free list head) */
    *(void **)first_out = *(void **)(data + DMOD_FREE_HEAD);

    /* Walk the free list, initialize each block, build allocated chain */
    int16_t remaining = count - 1;
    if (remaining >= 0) {
        uint8_t owner = (uint8_t)PROC1_$CURRENT;
        int16_t block_num = 1;

        do {
            uint8_t *block = *(uint8_t **)(data + DMOD_FREE_HEAD);

            /* Save next-free pointer before modifying block fields.
             * On m68k (32-bit), the pointer at offset 0x08 (4 bytes) does not
             * overlap with the status field at offset 0x0C. On 64-bit hosts,
             * reading void* at offset 0x08 reads 8 bytes which would overlap.
             * Hoisting the read preserves correct semantics on all platforms. */
            void *next_free = *(void **)(block + DISK_QBLK_FREE_NEXT);

            /* Initialize block fields */
            *(uint32_t *)(block + DISK_QBLK_STATUS) = 0;
            *(uint16_t *)(block + DISK_QBLK_FLAGS) = 0;
            *(block + DISK_QBLK_OWNER) = owner;
            *(block + DISK_QBLK_RESERVED) = 0;

            /* Record last block when we reach the count'th one */
            if (count == block_num) {
                *(void **)last_out = block;
            }

            /* Link: block->forward = block->free_next (build allocated chain) */
            *(void **)(block + DISK_QBLK_FORWARD) = next_free;
            /* Advance free list head to next block */
            *(void **)(data + DMOD_FREE_HEAD) = next_free;

            block_num++;
            remaining--;
        } while (remaining != -1);  /* dbf loop semantics */
    }

    /* Terminate the last allocated block */
    uint8_t *last_block = *(uint8_t **)last_out;
    *(uint32_t *)(last_block + DISK_QBLK_FREE_NEXT) = 0;
    *(uint32_t *)(last_block + DISK_QBLK_FORWARD) = 0;

    ML_$EXCLUSION_STOP((ml_$exclusion_t *)(data + DMOD_EXCLUSION));
}
