/*
 * DISK_$SORT - Sort I/O request queue by disk address
 *
 * Sorts a linked list of I/O requests by their logical block address
 * to optimize disk head movement (elevator algorithm).
 *
 * The device info at dev_entry+0x18 contains flags at offset +8 that
 * indicate whether to sort by LBA (at +0x3c) or by address (at +4).
 *
 * After sorting, the function also performs request coalescing for
 * sequential accesses on the same cylinder and head.
 *
 * @param dev_entry   Device entry pointer (volume entry + 0x7c)
 * @param queue_ptr   Pointer to queue head pointer
 */

#include "disk.h"

/* Request block offsets */
#define REQ_NEXT_OFFSET     0x00   /* Pointer to next request */
#define REQ_ADDR_OFFSET     0x04   /* Address (for SCSI sorting) */
#define REQ_CYL_OFFSET      0x04   /* Cylinder (word) */
#define REQ_HEAD_OFFSET     0x06   /* Head (byte) */
#define REQ_SECTOR_OFFSET   0x07   /* Sector (byte) */
#define REQ_LBA_OFFSET      0x3c   /* LBA for non-SCSI sorting */

/* Device flags */
#define DEV_FLAG_SCSI       0x200  /* Use address instead of LBA for sort */

/* Forward declaration for swap helper */
static void swap_requests(void);

void DISK_$SORT(void *dev_entry, void **queue_ptr)
{
    void **dev_info;
    uint16_t dev_flags;
    void *head;
    void *prev;
    void *curr;
    void *next;
    void *prev_sorted;
    int16_t coalesce_limit;
    uint32_t curr_key, next_key;

    head = *queue_ptr;
    prev_sorted = head;

    /* Get device info to check flags */
    dev_info = *(void ***)((uint8_t *)dev_entry + 0x18);
    dev_flags = *(uint16_t *)((uint8_t *)*dev_info + 8);

    /* Sort the queue using bubble sort */
    if ((dev_flags & DEV_FLAG_SCSI) == 0) {
        /* Sort by LBA (at offset +0x3c) */
        for (curr = head; curr != NULL; curr = *(void **)curr) {
            prev = curr;
            for (next = *(void **)curr; next != NULL; next = *(void **)prev) {
                next_key = *(uint32_t *)((uint8_t *)next + REQ_LBA_OFFSET);
                curr_key = *(uint32_t *)((uint8_t *)curr + REQ_LBA_OFFSET);

                if (next_key < curr_key) {
                    /* Swap curr and next */
                    void *tmp = *(void **)curr;
                    if (prev_sorted != NULL) {
                        *(void **)prev_sorted = next;
                    }
                    *(void **)curr = *(void **)next;
                    if (next == tmp) {
                        *(void **)next = curr;
                    } else {
                        *(void **)head = curr;
                        *(void **)next = tmp;
                    }

                    if (curr == head) {
                        head = next;
                    }
                    /* Swap pointers */
                    tmp = curr;
                    curr = next;
                    next = tmp;
                }
                prev = next;
            }
            prev_sorted = prev;
        }
    } else {
        /* Sort by address (at offset +4) for SCSI */
        for (curr = head; curr != NULL; curr = *(void **)curr) {
            prev = curr;
            for (next = *(void **)curr; next != NULL; next = *(void **)prev) {
                next_key = *(uint32_t *)((uint8_t *)next + REQ_ADDR_OFFSET);
                curr_key = *(uint32_t *)((uint8_t *)curr + REQ_ADDR_OFFSET);

                if (next_key < curr_key) {
                    /* Swap curr and next */
                    void *tmp = *(void **)curr;
                    if (prev_sorted != NULL) {
                        *(void **)prev_sorted = next;
                    }
                    *(void **)curr = *(void **)next;
                    if (next == tmp) {
                        *(void **)next = curr;
                    } else {
                        *(void **)head = curr;
                        *(void **)next = tmp;
                    }

                    if (curr == head) {
                        head = next;
                    }
                    /* Swap pointers */
                    tmp = curr;
                    curr = next;
                    next = tmp;
                }
                prev = next;
            }
            prev_sorted = prev;
        }
    }

    /* Coalesce sequential requests */
    coalesce_limit = *(int16_t *)((uint8_t *)dev_entry + 0x26);
    if (coalesce_limit != 1) {
        void *run_start = head;

        while (run_start != NULL) {
            next = *(void **)run_start;
            if (next != NULL) {
                int16_t start_cyl = *(int16_t *)((uint8_t *)run_start + REQ_CYL_OFFSET);
                uint8_t start_head = *(uint8_t *)((uint8_t *)run_start + REQ_HEAD_OFFSET);
                uint8_t start_sector = *(uint8_t *)((uint8_t *)run_start + REQ_SECTOR_OFFSET);

                int16_t next_cyl = *(int16_t *)((uint8_t *)next + REQ_CYL_OFFSET);
                uint8_t next_head = *(uint8_t *)((uint8_t *)next + REQ_HEAD_OFFSET);
                uint8_t next_sector = *(uint8_t *)((uint8_t *)next + REQ_SECTOR_OFFSET);

                /* Check if same cylinder, head, and within coalesce limit */
                if (start_cyl == next_cyl &&
                    start_head == next_head &&
                    (int16_t)(next_sector - start_sector) < coalesce_limit) {

                    /* Continue checking subsequent requests */
                    void *check = *(void **)next;
                    while (check != NULL) {
                        int16_t check_cyl = *(int16_t *)((uint8_t *)check + REQ_CYL_OFFSET);
                        uint8_t check_head = *(uint8_t *)((uint8_t *)check + REQ_HEAD_OFFSET);
                        uint8_t check_sector = *(uint8_t *)((uint8_t *)check + REQ_SECTOR_OFFSET);

                        if (start_cyl != check_cyl ||
                            start_head != check_head) {
                            break;
                        }

                        if ((int16_t)(check_sector - start_sector) >= coalesce_limit) {
                            /* TODO: Coalesce by calling FUN_00e3c370 */
                            break;
                        }

                        check = *(void **)check;
                    }
                }
            }
            run_start = next;
        }
    }

    *queue_ptr = head;
}
