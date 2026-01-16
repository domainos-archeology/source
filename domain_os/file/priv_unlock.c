/*
 * FILE_$PRIV_UNLOCK - Core file unlocking function
 *
 * Original address: 0x00E5FD32
 * Size: 1658 bytes
 *
 * This is the main internal function for all file unlock operations.
 * It handles:
 *   - Local file unlocks
 *   - Remote file unlocks (via REM_FILE_$UNLOCK)
 *   - Reference count management
 *   - Lock table cleanup
 *   - Data-time-valid tracking for modified files
 *
 * Parameters (from assembly analysis):
 *   file_uid     - UID of file to unlock (0x08,A6)
 *   lock_index   - Lock table index, 0 to search (0x0E,A6) - 16-bit in 32-bit space
 *   mode_asid    - High word=lock_mode, low word=ASID (0x10-0x12,A6)
 *   remote_flags - Remote operation flags, negative for remote (0x14,A6)
 *   param_5      - Context high (0x18,A6)
 *   param_6      - Context low/node address (0x1C,A6)
 *   dtv_out      - Data-time-valid output (0x20,A6)
 *   status_ret   - Output status (0x24,A6)
 */

#include "file/file_internal.h"
#include "ml/ml.h"
#include "netlog/netlog.h"

/*
 * Lock table base addresses (m68k target)
 */
#define LOT_BASE        ((file_lock_entry_detail_t *)0xE935B0)
#define LOT_ENTRY(n)    ((file_lock_entry_detail_t *)((uint8_t *)LOT_BASE + (n) * 0x1C))

/* Per-process lock table */
#define PROC_LOT_BASE   ((uint16_t *)0xE9F9CA)
#define PROC_LOT_ENTRY(asid, idx) \
    (*(uint16_t *)((uint8_t *)PROC_LOT_BASE + (asid) * 300 + (idx) * 2))
#define PROC_LOT_COUNT(asid) \
    (*(uint16_t *)((uint8_t *)0xEA3DC4 + (asid) * 2))

/*
 * FILE_$PRIV_UNLOCK - Core file unlocking function
 */
uint8_t FILE_$PRIV_UNLOCK(uid_t *file_uid, uint16_t lock_index,
                          uint32_t mode_asid, int32_t remote_flags,
                          int32_t param_5, int32_t param_6,
                          uint32_t *dtv_out, status_$t *status_ret)
{
    int16_t asid = (int16_t)(mode_asid & 0xFFFF);
    uint16_t lock_mode = (uint16_t)(mode_asid >> 16);
    int16_t hash_index;
    uint16_t found_entry = 0;
    uint16_t proc_slot = 0;
    int16_t prev_entry;
    file_lock_entry_detail_t *entry;
    status_$t local_status = 0;
    uint8_t result_flags = 0;
    int8_t done_flag = 0;
    uint8_t modified_flag = 0;

    /* Locals for entry info */
    uid_t local_uid;
    uint32_t entry_node_high, entry_node_low;
    uint8_t entry_flags;
    int8_t is_pending;
    uint16_t entry_seq;
    uint32_t entry_context;
    uint16_t entry_side;
    uint8_t entry_mode;
    int8_t has_other_locks;
    int8_t has_exclusive;
    char attr_buf[100];

    *dtv_out = 0;
    result_flags = 0;
    done_flag = 0;

    /* Compute hash for lookup */
    hash_index = UID_$HASH(file_uid, NULL);

    /*
     * Skip unlock for special modes 8 and 9
     * These are pseudo-locks that don't require actual unlock
     */
    if ((remote_flags >= 0 || lock_mode == 8) && lock_mode != 9) {
        int proc_table_base = asid * 300 + 0xE9F9CA - 0x2662;

        /*
         * Main unlock loop - may retry for mode 0 (unlock all of type)
         */
        while (1) {
            proc_slot = 0;
            ML_$LOCK(5);

            if (remote_flags < 0) {
                /*
                 * Remote unlock - search by hash
                 */
                found_entry = FILE_$LOT_HASHTAB[hash_index];
                while (found_entry > 0) {
                    entry = LOT_ENTRY(found_entry);

                    /* Match by mode (or any if mode==0), sequence, context, and node */
                    if (((lock_mode == 0) ||
                         (((entry->flags2 & 0x78) >> 3) == lock_mode)) &&
                        ((mode_asid >> 16) == 0 ||
                         (entry->sequence == (uint16_t)(mode_asid >> 16))) &&
                        (entry->node_low == param_6) &&
                        (entry->context == param_5) &&
                        ((entry->flags2 & 4) == 0) &&
                        (entry->uid_high == file_uid->high) &&
                        (entry->uid_low == file_uid->low) &&
                        (entry->refcount != 0)) {
                        break;
                    }
                    found_entry = entry->next;
                }

                if (found_entry == 0) {
                    if (lock_mode == 8) {
                        local_status = status_$file_object_not_locked_by_this_process;
                    } else {
                        local_status = 0;
                    }
                    goto done_unlock;
                }

                /*
                 * Handle mode 8 - set delete-on-unlock flag
                 */
                if (lock_mode == 8) {
                    local_uid.high = file_uid->high;
                    local_uid.low = file_uid->low;

                    AST_$GET_COMMON_ATTRIBUTES((void *)&local_uid, 0x10,
                                                attr_buf, &local_status);
                    if (local_status == 0 && attr_buf[0] == 0) {
                        uint16_t flag_val = 1;
                        AST_$SET_ATTRIBUTE(file_uid, 7, &flag_val, &local_status);
                    }
                    ML_$UNLOCK(5);
                    goto done_logging;
                }
            } else if (lock_index == 0) {
                /*
                 * Local unlock with search - find matching lock in process table
                 */
                int16_t count = PROC_LOT_COUNT(asid) - 1;
                if (count >= 0) {
                    uint16_t slot = 1;
                    for (int i = 0; i <= count; i++, slot++) {
                        found_entry = PROC_LOT_ENTRY(asid, slot);
                        if (found_entry != 0) {
                            entry = LOT_ENTRY(found_entry);

                            /* Check for matching mode or any if mode==0 */
                            if (((lock_mode == 0) || ((entry->flags2 & 1) != 0)) &&
                                (entry->uid_high == file_uid->high) &&
                                (entry->uid_low == file_uid->low) &&
                                ((((entry->flags2 & 0x78) >> 3) == lock_mode) ||
                                 (lock_mode == 0))) {
                                proc_slot = slot;
                                break;
                            }
                        }
                        found_entry = 0;
                    }
                }

                if (proc_slot == 0) {
                    local_status = status_$file_object_not_locked_by_this_process;
                    goto done_unlock;
                }
            } else if (lock_index <= 0x96) {
                /*
                 * Local unlock with explicit index
                 */
                found_entry = PROC_LOT_ENTRY(asid, lock_index);
                if (found_entry != 0) {
                    entry = LOT_ENTRY(found_entry);

                    /* Validate entry matches request */
                    if ((entry->uid_high == file_uid->high) &&
                        (entry->uid_low == file_uid->low) &&
                        ((lock_mode == 0) ||
                         ((((entry->flags2 & 0x78) >> 3) == lock_mode) &&
                          ((entry->flags2 & 1) == 0)))) {
                        proc_slot = lock_index;
                    } else {
                        found_entry = 0;
                    }
                }

                if (found_entry == 0) {
                    local_status = status_$file_object_not_locked_by_this_process;
                    goto done_unlock;
                }
            } else {
                local_status = status_$file_invalid_arg;
                goto done_unlock;
            }

            /*
             * Found lock entry - clear from process table and process unlock
             */
            if (proc_slot != 0) {
                PROC_LOT_ENTRY(asid, proc_slot) = 0;
            }

            /*
             * Process the unlock
             */
            done_flag = -1;

            /* Set modification timestamp */
            modified_flag = AST_$SET_DTS(0x10, file_uid, NULL, NULL, &local_status);

            entry = LOT_ENTRY(found_entry);

            /* Save entry info for later */
            local_uid.high = file_uid->high;
            local_uid.low = file_uid->low;
            entry_node_high = entry->node_high;
            entry_node_low = entry->node_low;
            entry_flags = ((entry->flags2 & 4) ? 0x80 : 0) | 0x40;
            is_pending = ((entry->flags2 & 2) == 0) ? -1 : 0;
            entry_seq = entry->sequence;
            entry_context = entry->context;
            entry_side = entry->flags2 >> 7;
            entry_mode = (entry->flags2 & 0x78) >> 3;

            int8_t is_exclusive = ((entry_mode == 4) || (entry_mode == 0x0B)) ? -1 : 0;

            /* Decrement reference count */
            entry->refcount--;
            if (entry->refcount != 0) {
                goto done_unlock;
            }

            /*
             * Reference count is 0 - remove from hash chain
             */
            has_exclusive = 0;
            has_other_locks = 0;
            prev_entry = 0;
            int16_t current = FILE_$LOT_HASHTAB[hash_index];

            while (current > 0) {
                file_lock_entry_detail_t *curr_entry = LOT_ENTRY(current);
                int16_t next = curr_entry->next;

                if ((curr_entry->uid_high == file_uid->high) &&
                    (curr_entry->uid_low == file_uid->low)) {

                    if (current == found_entry) {
                        /* Remove this entry from chain */
                        if (prev_entry == 0) {
                            FILE_$LOT_HASHTAB[hash_index] = entry->next;
                        } else {
                            LOT_ENTRY(prev_entry)->next = entry->next;
                        }
                        /* Add to free list */
                        entry->next = FILE_$LOT_FREE;
                        FILE_$LOT_FREE = found_entry;
                    } else {
                        /* Another lock on same file */
                        has_other_locks = -1;
                        uint8_t other_mode = (curr_entry->flags2 & 0x78) >> 3;
                        if (other_mode == 4 || other_mode == 0x0B) {
                            has_exclusive = -1;
                        }
                    }
                }

                prev_entry = current;
                current = next;
            }

            /*
             * If was exclusive lock and no other exclusive locks remain,
             * purify the file
             */
            if ((is_exclusive & ~has_exclusive) && (*(uint8_t *)&file_uid->high != 0)) {
                AST_$PURIFY(file_uid, 0x8000, 0, NULL, 0, &local_status);

                if ((has_other_locks >= 0) && ((entry_flags & 0x80) == 0)) {
                    uint32_t zero = 0;
                    AST_$SET_ATTRIBUTE(file_uid, 0x0B, &zero, &local_status);
                }
            }

            /*
             * Get data-time-valid if requested
             */
            if ((remote_flags & is_exclusive) && (*(uint8_t *)&file_uid->high != 0)) {
                AST_$GET_DTV(file_uid, 0, dtv_out, &local_status);
                if (local_status != 0) {
                    *dtv_out = 0;
                }
            }

            ML_$UNLOCK(5);

            /*
             * Handle remote unlock if needed
             */
            if (entry_flags & 0x80) {
                /* Remote lock - call REM_FILE_$UNLOCK */
                int8_t truncate_flag = 0;

                if (is_pending < 0 && has_other_locks >= 0) {
                    /* Check if file needs local read lock */
                    AST_$GET_COMMON_ATTRIBUTES((void *)&local_uid, 0x30,
                                                attr_buf, &local_status);
                    if (local_status == 0 && *(int16_t *)&attr_buf[20] == 0) {
                        REM_FILE_$LOCAL_READ_LOCK((void *)&entry_node_high,
                                                   (void *)&local_uid,
                                                   attr_buf + 32, &local_status);
                        if (local_status == status_$file_object_not_locked_by_this_process) {
                            truncate_flag = -1;
                        }
                    }

                    if (is_pending < 0) {
                        entry_mode = FILE_$LOCK_MAP_TABLE[entry_mode];
                    }
                }

                result_flags = REM_FILE_$UNLOCK((void *)&local_uid, entry_mode,
                                                 entry_context, entry_seq, NODE_$ME,
                                                 modified_flag, &local_status);

                if (local_status == 0) {
                    local_status = *(status_$t *)&attr_buf[0];
                }

                if (local_status == 0) {
                    if (truncate_flag < 0) {
                        AST_$TRUNCATE(file_uid, 0, 1, &result_flags, &local_status);
                    } else if (result_flags & 0x80) {
                        uint32_t zero = 0;
                        AST_$COND_FLUSH(file_uid, &zero, &local_status);
                    }
                }
            } else {
                /* Local lock - truncate if file was modified and no other locks */
                if ((has_other_locks >= 0) && (*(uint8_t *)&file_uid->high != 0)) {
                    AST_$TRUNCATE(file_uid, 0, 1, &result_flags, &local_status);
                }
            }

            /*
             * If mode is 0 (unlock all), continue loop
             */
            if (local_status != 0 || lock_mode != 0) {
                break;
            }
        }  /* end while loop */

done_unlock:
        ML_$UNLOCK(5);
    }

done_logging:
    /*
     * Log if enabled
     */
    if ((NETLOG_$OK_TO_LOG < 0) && (local_status == 0)) {
        NETLOG_$LOG_IT(0x13, file_uid, 0, 0, entry_side, lock_mode,
                       (entry_flags & 0x80) ? 1 : 0,
                       (remote_flags < 0) ? 1 : 0);
    }

    /*
     * Map certain error codes to success
     */
    if (local_status == status_$file_object_not_locked_by_this_process && done_flag < 0) {
        *status_ret = 0;
        return result_flags & 1;
    }

    *status_ret = local_status;
    return (local_status == 0) ? (result_flags & 1) : 0;
}
