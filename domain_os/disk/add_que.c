/*
 * DISK_$ADD_QUE - Add requests to disk I/O queue
 *
 * Adds one or more I/O requests to the disk queue. Performs sorting
 * and request coalescing to optimize disk access patterns.
 *
 * This function:
 * 1. Optionally pre-sorts the request list by LBA
 * 2. Acquires the disk lock
 * 3. Counts requests and tracks which ones pass the current disk position
 * 4. Groups consecutive sector requests together
 * 5. Merges requests into the appropriate queue position
 *
 * @param flags       Flags (bit 0: skip initial sort)
 * @param dev_entry   Device entry pointer (volume entry + 0x7c)
 * @param queue       Queue control structure
 * @param req_list    Linked list of requests to add
 */

#include "disk.h"

/* Request block offsets */
#define REQ_NEXT_OFFSET     0x00   /* Pointer to next request */
#define REQ_CYL_OFFSET      0x04   /* Cylinder (word) */
#define REQ_HEAD_OFFSET     0x06   /* Head (byte) */
#define REQ_SECTOR_OFFSET   0x07   /* Sector (byte) */
#define REQ_COUNT_OFFSET    0x1c   /* Count/group size */
#define REQ_GROUP_END       0x18   /* Pointer to last in group */
#define REQ_LBA_OFFSET      0x3c   /* Logical block address */

/* Device flags */
#define DEV_FLAG_SCSI       0x200  /* Uses SCSI addressing */

/* Queue structure offsets */
#define QUEUE_LOCK_OFFSET   0x0a   /* Lock ID */
#define QUEUE_POS_OFFSET    0x04   /* Current position */
#define QUEUE_HEAD_OFFSET   0x08   /* Queue head */
#define QUEUE_TAIL_OFFSET   0x0c   /* Queue tail */

/* Error message for unsupported queued drivers */
extern void *Disk_Queued_Drivers_Not_Supported_Err;

/* External functions */
extern void CRASH_SYSTEM(void *error);
/* ML_$SPIN_LOCK, ML_$SPIN_UNLOCK declared in ml/ml.h via disk.h */

/* Queue data at 0xe7a1cc */
#define QUEUE_DATA_BASE  ((uint8_t *)0x00e7a1cc)
#define QUEUE_POSITION_ARRAY  (QUEUE_DATA_BASE + 0xb08)

/* Internal queue merge functions */
static void merge_to_front(void *queue, int16_t count, uint8_t flag);
static void merge_to_back(void *queue, int16_t count, uint8_t flag);

void DISK_$ADD_QUE(uint16_t flags, void *dev_entry, void *queue, void *req_list)
{
    void **dev_info;
    uint16_t dev_flags;
    int16_t lock_id;
    uint16_t lock_token;
    void *req;
    void *next;
    void *prev;
    void *head;
    void *group_start = NULL;
    void *group_end = NULL;
    uint32_t *position_array;
    uint32_t current_pos;
    uint16_t total_count;
    uint16_t ahead_count;
    int16_t coalesce_limit;
    int8_t needs_sort;

    /* Get device info and check for queued driver support */
    dev_info = *(void ***)((uint8_t *)dev_entry + 0x18);
    dev_flags = *(uint16_t *)((uint8_t *)*dev_info + 8);

    if ((dev_flags & DEV_FLAG_SCSI) != 0) {
        CRASH_SYSTEM(&Disk_Queued_Drivers_Not_Supported_Err);
    }

    /* Get coalesce limit from device entry */
    coalesce_limit = *(int16_t *)((uint8_t *)dev_entry + 0x1c);
    needs_sort = -((flags & 1) != 0);

sort_again:
    /* Sort request list by LBA if needed */
    if (needs_sort >= 0) {
        void *sorted_prev = NULL;
        head = req_list;

        for (req = head; req != NULL; req = *(void **)req) {
            prev = req;
            for (next = *(void **)req; next != NULL; next = *(void **)prev) {
                uint32_t next_lba = *(uint32_t *)((uint8_t *)next + REQ_LBA_OFFSET);
                uint32_t req_lba = *(uint32_t *)((uint8_t *)req + REQ_LBA_OFFSET);

                if (next_lba < req_lba) {
                    /* Swap */
                    void *tmp = *(void **)req;
                    if (sorted_prev != NULL) {
                        *(void **)sorted_prev = next;
                    }
                    *(void **)req = *(void **)next;
                    if (next == tmp) {
                        *(void **)next = req;
                    } else {
                        *(void **)head = req;
                        *(void **)next = tmp;
                    }

                    if (req == req_list) {
                        req_list = next;
                    }
                    tmp = req;
                    req = next;
                    next = tmp;
                }
                prev = next;
            }
            sorted_prev = prev;
        }
    }

    /* Acquire disk lock */
    lock_id = *(int16_t *)((uint8_t *)dev_entry + QUEUE_LOCK_OFFSET);
    ML_$LOCK(lock_id);

    /* Initialize counters */
    total_count = 0;
    ahead_count = 0;

    /* Get current disk position */
    current_pos = (*(uint32_t *)((uint8_t *)queue + QUEUE_POS_OFFSET) & 0xffff0) >> 4;

    /* Clear position tracking array */
    *(uint32_t *)QUEUE_POSITION_ARRAY = 0;

    position_array = (uint32_t *)QUEUE_DATA_BASE;

    /* Walk request list to count and group requests */
    for (req = req_list; req != NULL; req = *(void **)req) {
        group_start = req;
        group_end = req;

        total_count++;
        position_array++;
        *position_array = (uint32_t)(uintptr_t)req;

        /* Track first request past current position */
        if (ahead_count == 0) {
            uint16_t req_pos = *(uint16_t *)((uint8_t *)req + REQ_CYL_OFFSET);
            if (current_pos <= req_pos) {
                ahead_count = total_count;
            }
        }

        /* Initialize group size to 1 */
        *(int16_t *)((uint8_t *)req + REQ_COUNT_OFFSET) = 1;

        /* Group consecutive sectors on same cylinder/head */
        for (next = *(void **)req; next != NULL; next = *(void **)group_end) {
            /* Check if sort order violated - need to re-sort */
            if (needs_sort < 0) {
                uint32_t end_lba = *(uint32_t *)((uint8_t *)group_end + REQ_LBA_OFFSET);
                uint32_t next_lba = *(uint32_t *)((uint8_t *)next + REQ_LBA_OFFSET);
                if (next_lba < end_lba) {
                    needs_sort = 0;
                    ML_$UNLOCK(lock_id);
                    goto sort_again;
                }
            }

            /* Check if same cylinder */
            int16_t req_cyl = *(int16_t *)((uint8_t *)req + REQ_CYL_OFFSET);
            int16_t next_cyl = *(int16_t *)((uint8_t *)next + REQ_CYL_OFFSET);
            if (req_cyl != next_cyl) {
                /* Store group end pointer if group > 1 */
                if (*(int16_t *)((uint8_t *)group_start + REQ_COUNT_OFFSET) != 1) {
                    *(void **)((uint8_t *)group_start + REQ_GROUP_END) = group_end;
                }
                break;
            }

            /* Check if consecutive sector within coalesce limit */
            uint8_t end_sector = *(uint8_t *)((uint8_t *)group_end + REQ_SECTOR_OFFSET);
            uint8_t next_sector = *(uint8_t *)((uint8_t *)next + REQ_SECTOR_OFFSET);
            int16_t sector_diff = (int16_t)next_sector - (int16_t)end_sector;

            if ((int32_t)((uintptr_t)group_end + (int32_t)coalesce_limit) ==
                *(int32_t *)((uint8_t *)next + REQ_LBA_OFFSET)) {
                /* Same group - increment count */
                (*(int16_t *)((uint8_t *)group_start + REQ_COUNT_OFFSET))++;
            } else {
                /* Store group end pointer if group > 1 */
                if (*(int16_t *)((uint8_t *)group_start + REQ_COUNT_OFFSET) != 1) {
                    *(void **)((uint8_t *)group_start + REQ_GROUP_END) = group_end;
                }
                /* Start new group */
                *(int16_t *)((uint8_t *)next + REQ_COUNT_OFFSET) = 1;
                group_start = next;
            }
            group_end = next;
        }

        /* Store final group end pointer */
        *(void **)((uint8_t *)req + REQ_GROUP_END) = group_end;
    }

    /* Store final group end pointer for last request */
    if (*(int16_t *)((uint8_t *)group_start + REQ_COUNT_OFFSET) != 1) {
        *(void **)((uint8_t *)group_start + REQ_GROUP_END) = group_end;
    }

    /* Determine merge position based on ahead_count */
    uint16_t merge_count;
    if (ahead_count == 0) {
        merge_count = total_count;
    } else {
        uint16_t check_pos = *(uint16_t *)(QUEUE_POSITION_ARRAY + ahead_count * 4 + REQ_CYL_OFFSET);
        if (check_pos == current_pos) {
            merge_count = ahead_count;
        } else {
            merge_count = ahead_count - 1;
        }
    }

    /* Clear end of position array */
    *(uint32_t *)(QUEUE_DATA_BASE + (total_count + 1) * 4 + 0xb0c) = 0;

    /* Acquire spin lock for queue manipulation */
    lock_token = ML_$SPIN_LOCK(queue);

    /* Determine merge strategy based on current queue state */
    int16_t queue_pos_flag = *(int16_t *)((uint8_t *)queue + 4);

    if (queue_pos_flag < 0) {
        /* Queue is draining - merge ahead requests to front */
        /* TODO: Complex merge logic */
    } else {
        /* Queue is filling - merge to back */
        /* TODO: Complex merge logic */
    }

    /* Release spin lock */
    ML_$SPIN_UNLOCK(queue, lock_token);
}

/*
 * Note: FUN_00e3c690 and FUN_00e3c5da are internal merge helpers
 * that manipulate the queue links. Their full implementation
 * requires more reverse engineering.
 */
