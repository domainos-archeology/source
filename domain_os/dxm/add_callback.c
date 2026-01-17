/*
 * dxm/add_callback.c - DXM_$ADD_CALLBACK implementation
 *
 * Adds a callback to a deferred execution queue.
 *
 * Original address: 0x00E16FE0
 */

#include "dxm/dxm_internal.h"

/*
 * DXM_$ADD_CALLBACK - Add a callback to a deferred execution queue
 *
 * Adds a callback function with associated data to a DXM queue.
 * The callback will be executed later by a helper process.
 *
 * Parameters:
 *   queue - Queue to add callback to
 *   callback - Pointer to callback function pointer
 *   data - Pointer to pointer to data (data is copied from *data)
 *   flags - Packed flags:
 *           Low 16 bits: Data size (0-12 bytes)
 *           Byte at bit 16-23: If negative (bit 23 set), check for duplicates
 *   status_ret - Status return
 *
 * If the check duplicates flag is set, the function scans the queue
 * for an existing entry with the same callback and data bytes. If
 * found, no new entry is added (deduplication).
 *
 * If the queue is full, the function crashes with an error message.
 *
 * Assembly highlights:
 *   - Data size limited to 12 bytes max (crashes if > 12)
 *   - Data is copied in 4-byte chunks to local storage
 *   - Duplicate check compares callback and data byte-by-byte
 *   - Queue full causes CRASH_SYSTEM call (but continues after?)
 *   - After adding entry, signals event count to wake helper
 */
void DXM_$ADD_CALLBACK(dxm_queue_t *queue, void **callback, void **data,
                       uint32_t flags, status_$t *status_ret)
{
    uint16_t data_size;
    int8_t check_dup;
    uint16_t token;
    uint16_t idx;
    uint16_t next_tail;
    int16_t num_words;
    int16_t i;
    dxm_entry_t *entry;
    uint32_t local_data[3];  /* Local copy of callback data (12 bytes max) */
    int found_dup;

    *status_ret = status_$ok;

    /* Extract flags: low 16 bits = data size, byte at offset 2 = dup check */
    data_size = (uint16_t)(flags & 0xFFFF);
    check_dup = (int8_t)((flags >> 16) & 0xFF);

    /* Validate data size */
    if (data_size > DXM_MAX_DATA_SIZE) {
        CRASH_SYSTEM((status_$t *)DXM_Datum_too_large_err);
    }

    /* Copy data to local storage in 4-byte chunks
     * num_words = (data_size + 3) / 4 = number of 32-bit words */
    num_words = (int16_t)((data_size + 3) >> 2);
    if (num_words != 0) {
        uint8_t *src = (uint8_t *)*data;
        for (i = 0; i < num_words; i++) {
            /* Copy 4 bytes at a time, reading from src */
            local_data[i] = *(uint32_t *)(src + i * 4);
        }
    }

    /* Lock the queue */
    token = ML_$SPIN_LOCK(&queue->lock);

    /* If check_dup flag is negative (bit 7 set), scan for duplicates */
    if (check_dup < 0) {
        for (idx = queue->head; idx != queue->tail; idx = (idx + 1) & queue->mask) {
            entry = (dxm_entry_t *)((char *)queue->entries + ((int16_t)idx << 4));

            /* Check if callback matches */
            if (entry->callback == (void (*)(void *))*callback) {
                /* Callback matches, check data bytes */
                found_dup = 1;
                if (data_size != 0) {
                    uint8_t *entry_data = entry->data;
                    uint8_t *local_bytes = (uint8_t *)local_data;
                    for (i = 0; i < (int16_t)data_size; i++) {
                        if (local_bytes[i] != entry_data[i]) {
                            found_dup = 0;
                            break;
                        }
                    }
                }
                if (found_dup) {
                    /* Duplicate found, don't add new entry */
                    goto unlock_and_return;
                }
            }
        }
    }

    /* Calculate next tail position */
    next_tail = (queue->tail + 1) & queue->mask;

    /* Check if queue is full */
    if (next_tail == queue->head) {
        /* Queue is full */
        *status_ret = status_$dxm_no_more_deferred_execution_queue_slots;
        CRASH_SHOW_STRING(DXM_No_room_err);
        CRASH_SYSTEM(status_ret);
        DXM_$OVERRUNS++;
        goto unlock_and_return;
    }

    /* Add entry to queue at current tail position */
    entry = (dxm_entry_t *)((char *)queue->entries + ((int16_t)queue->tail << 4));

    /* Store callback */
    entry->callback = (void (*)(void *))*callback;

    /* Copy data words */
    if (num_words != 0) {
        uint32_t *dst = (uint32_t *)entry->data;
        for (i = 0; i < num_words; i++) {
            dst[i] = local_data[i];
        }
    }

    /* Advance tail */
    queue->tail = next_tail;

    /* Unlock and signal */
    ML_$SPIN_UNLOCK(&queue->lock, token);

    /* Signal event count to wake helper process */
    EC_$ADVANCE_WITHOUT_DISPATCH(&queue->ec);
    return;

unlock_and_return:
    ML_$SPIN_UNLOCK(&queue->lock, token);
}
