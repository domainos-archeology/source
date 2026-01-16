/*
 * DBUF_$GET_BLOCK - Get a disk block into a buffer
 *
 * Retrieves a disk block into a memory buffer, reading from disk if necessary.
 * Uses an LRU cache with spin lock protection.
 *
 * Original address: 0x00e3a5b0
 */

#include "dbuf/dbuf_internal.h"

/* Forward declaration for NETLOG (if logging is enabled) */
extern char NETLOG_$OK_TO_LOG;
extern void NETLOG_$LOG_IT(uint16_t type, uid_t *uid, int16_t p1, uint16_t p2,
                           uint16_t p3, uint16_t p4, uint16_t p5, uint16_t p6);

/*
 * DBUF_$GET_BLOCK
 *
 * This function implements an LRU buffer cache. It:
 *   1. Searches for the requested block in the cache
 *   2. If found, moves it to the head (MRU) and returns it
 *   3. If not found, finds a free buffer (or flushes an old one)
 *   4. Reads the block from disk into the buffer
 *   5. If all buffers are busy, waits for one to become free
 *
 * Parameters:
 *   vol_idx     - Volume index (0-7)
 *   block       - Disk block number to read
 *   uid         - Expected UID for validation
 *   block_hint  - Block hint/type for allocation
 *   flags       - Flags (bit 4: update UID, bit 5: allow stopped)
 *   status      - Receives status code
 *
 * Returns:
 *   Pointer to buffer data, or NULL on error
 */
void *DBUF_$GET_BLOCK(uint16_t vol_idx, int32_t block, uid_t *uid,
                      uint32_t block_hint, uint32_t flags,
                      status_$t *status)
{
    uint16_t token;
    uint32_t wait_value;
    dbuf_$entry_t *entry;
    dbuf_$entry_t *victim;
    dbuf_$write_params_t write_params;
    uid_t local_uid;
    uint32_t local_hint;
    int i;
    uint8_t *src;
    uint8_t *dst;
    uint8_t update_flag;

    *status = status_$ok;

    /* Optional logging */
    if (NETLOG_$OK_TO_LOG < 0) {
        NETLOG_$LOG_IT(0x10, uid, (int16_t)(block_hint >> 5),
                       (uint16_t)(block_hint & 0x1F),
                       (uint16_t)flags, vol_idx, 0, 0);
    }

retry:
    /* Acquire spin lock */
    token = ML_$SPIN_LOCK(&DBUF_SPIN_LOCK);

    /* Calculate wait value for event count */
    wait_value = dbuf_$eventcount.value + 1;

    /*
     * Search for the block in the buffer cache
     */
    entry = dbuf_$head;
    while (entry != NULL) {
        update_flag = (uint8_t)(flags >> 16);

        /* Check if this entry matches our request */
        if (entry->block == block && DBUF_GET_VOL(entry) == vol_idx) {
            /* Found matching buffer */

            /* If buffer is busy (I/O in progress), wait for it */
            if (entry->flags & DBUF_ENTRY_BUSY) {
                goto wait_for_buffer;
            }

            /* Increment reference count */
            entry->ref_count++;

            /* Update UID if flag 0x10 is set */
            if (flags & 0x10) {
                entry->uid.high = uid->high;
                entry->uid.low = uid->low;
                entry->hint = block_hint;
                entry->type = update_flag;
            }

            /* Move to head of LRU list if not already there */
            if (entry->prev != NULL) {
                /* Remove from current position */
                entry->prev->next = entry->next;
                if (entry->next != NULL) {
                    entry->next->prev = entry->prev;
                }
                /* Insert at head */
                dbuf_$head->prev = entry;
                entry->next = dbuf_$head;
                entry->prev = NULL;
                dbuf_$head = entry;
            }

            /* Release spin lock and return buffer */
            ML_$SPIN_UNLOCK(&DBUF_SPIN_LOCK, token);
            return entry->data;
        }

        entry = entry->next;
    }

    /*
     * Block not found in cache - need to allocate a buffer.
     * Search for an unreferenced buffer (ref_count == 0).
     * Start from the tail (LRU end) of the list.
     */
    victim = dbuf_$head;
    while (victim != NULL) {
        /* Skip if ref_count > 0 or buffer is busy */
        if (victim->ref_count == 0 && !(victim->flags & DBUF_ENTRY_BUSY)) {
            break;
        }
        victim = victim->next;
    }

    /* No free buffer found - need to wait */
    if (victim == NULL) {
        goto wait_for_buffer;
    }

    /*
     * Check if victim buffer is dirty and needs writeback
     */
    if (DBUF_IS_VALID(victim)) {
        /* Mark buffer as busy for writeback */
        victim->flags |= DBUF_ENTRY_BUSY;
        ML_$SPIN_UNLOCK(&DBUF_SPIN_LOCK, token);

        /* Copy write params from victim buffer */
        src = (uint8_t *)&victim->uid;
        dst = (uint8_t *)&write_params;
        for (i = 0; i < 12; i++) {
            *dst++ = *src++;
        }
        write_params.type = victim->type;
        write_params.reserved = 0;

        /* Clear dirty flag */
        victim->flags &= ~DBUF_ENTRY_DIRTY;

        /* Write dirty buffer to disk
         * DISK_$WRITE params: vol_idx, block_num (as void*), ppn (as void*), uid_params, status
         */
        DISK_$WRITE(DBUF_GET_VOL(victim), (void *)(uintptr_t)victim->block,
                    (void *)(uintptr_t)victim->ppn, &write_params, status);

        if (*status != status_$ok) {
            /* Mark volume as having trouble but continue */
            DBUF_$TROUBLE |= (1 << DBUF_GET_VOL(victim));
            *status = status_$ok;
        }

        /* Reacquire lock and clear busy flag */
        token = ML_$SPIN_LOCK(&DBUF_SPIN_LOCK);
        victim->flags &= ~DBUF_ENTRY_BUSY;

        /* Wake any waiters */
        if (dbuf_$waiters != 0) {
            ML_$SPIN_UNLOCK(&DBUF_SPIN_LOCK, token);
            EC_$ADVANCE(&dbuf_$eventcount);
        } else {
            ML_$SPIN_UNLOCK(&DBUF_SPIN_LOCK, token);
        }

        /* Retry from the beginning */
        goto retry;
    }

    /*
     * Found a clean, unreferenced buffer - use it for our block.
     * Move to head of LRU list.
     */
    if (victim->prev != NULL) {
        victim->prev->next = victim->next;
        if (victim->next != NULL) {
            victim->next->prev = victim->prev;
        }
        dbuf_$head->prev = victim;
        victim->next = dbuf_$head;
        victim->prev = NULL;
        dbuf_$head = victim;
    }

    /* Set up buffer for new block */
    DBUF_SET_VOL(victim, vol_idx);
    victim->block = block;
    victim->uid.high = uid->high;
    victim->uid.low = uid->low;
    victim->hint = block_hint;
    victim->type = (uint8_t)(flags >> 16);
    victim->flags |= DBUF_ENTRY_BUSY;

    ML_$SPIN_UNLOCK(&DBUF_SPIN_LOCK, token);

    /* If flag 0x10 is set, skip disk read (caller will fill buffer) */
    if (flags & 0x10) {
        goto finish_setup;
    }

    /* Copy UID to local for disk read */
    local_uid.high = uid->high;
    local_uid.low = uid->low;
    local_hint = block_hint;

    /* Read block from disk
     * DISK_$READ params: vol_idx, block_num (as void*), ppn (as void*), uid_params, status
     */
    DISK_$READ(vol_idx, (void *)(uintptr_t)block, (void *)(uintptr_t)victim->ppn,
               &local_uid, status);

    if (*status != status_$ok) {
        /* Check if error can be ignored (flag 0x20 and stopped status) */
        if ((flags & 0x20) && *status == status_$storage_module_stopped) {
            /* Continue despite error */
            goto finish_setup;
        }

        /* Clear buffer and return error */
        DBUF_SET_VOL(victim, 0);
        victim->block = -1;

        token = ML_$SPIN_LOCK(&DBUF_SPIN_LOCK);
        victim->flags &= ~DBUF_ENTRY_BUSY;

        if (dbuf_$waiters != 0) {
            ML_$SPIN_UNLOCK(&DBUF_SPIN_LOCK, token);
            EC_$ADVANCE(&dbuf_$eventcount);
        } else {
            ML_$SPIN_UNLOCK(&DBUF_SPIN_LOCK, token);
        }

        /* Set error bit 7 and return NULL */
        *(uint8_t *)status |= 0x80;
        return NULL;
    }

finish_setup:
    /* Clear busy flag and increment ref count */
    token = ML_$SPIN_LOCK(&DBUF_SPIN_LOCK);
    victim->flags &= ~DBUF_ENTRY_BUSY;

    /* Wake any waiters */
    if (dbuf_$waiters != 0) {
        ML_$SPIN_UNLOCK(&DBUF_SPIN_LOCK, token);
        EC_$ADVANCE(&dbuf_$eventcount);
    } else {
        ML_$SPIN_UNLOCK(&DBUF_SPIN_LOCK, token);
    }

    victim->ref_count = 1;
    return victim->data;

wait_for_buffer:
    /* Increment waiter count */
    dbuf_$waiters++;
    ML_$SPIN_UNLOCK(&DBUF_SPIN_LOCK, token);

    /* Wait for a buffer to become available
     * EC_$WAIT takes array of 3 EC pointers and pointer to wait value
     */
    {
        ec_$eventcount_t *ec_array[3];
        int32_t wait_val;

        ec_array[0] = &dbuf_$eventcount;
        ec_array[1] = NULL;
        ec_array[2] = NULL;
        wait_val = (int32_t)wait_value;

        EC_$WAIT(ec_array, &wait_val);
    }

    /* Decrement waiter count */
    dbuf_$waiters--;

    /* Retry */
    goto retry;
}
