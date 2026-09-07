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

#include "disk/disk_internal.h"

/* Request block offsets */
#define REQ_NEXT_OFFSET     0x00   /* VA of the next request (disk_io_req_t.next) */
#define REQ_ADDR_OFFSET     0x04   /* Address (for SCSI sorting) */
#define REQ_CYL_OFFSET      0x04   /* Cylinder (word) */
#define REQ_HEAD_OFFSET     0x06   /* Head (byte) */
#define REQ_SECTOR_OFFSET   0x07   /* Sector (byte) */
#define REQ_LBA_OFFSET      0x3c   /* LBA for non-SCSI sorting */

/* Device flags */
#define DEV_FLAG_SCSI       0x200  /* Use address instead of LBA for sort */

/*
 * The request chain link at +0x00 is a four-byte cell holding a target
 * virtual address, not a host pointer: the original moves it with `move.l`
 * and disk_io_req_t.daddr (+0x04) sits immediately above it.  Read and write
 * it through these two accessors so a 64-bit host build does not overrun
 * daddr (bead source-wyn9; cells cited at 0x00E3BE8A / 0x00E3D50E).
 * ARCH_VA_TO_PTR / ARCH_PTR_TO_VA are identity casts on m68k.
 */
static inline void *req_next(const void *req)
{
    return ARCH_VA_TO_PTR(*(const uint32_t *)((const uint8_t *)req +
                                              REQ_NEXT_OFFSET));
}

static inline void req_set_next(void *req, void *val)
{
    *(uint32_t *)((uint8_t *)req + REQ_NEXT_OFFSET) = ARCH_PTR_TO_VA(val);
}

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
        for (curr = head; curr != NULL; curr = req_next(curr)) {
            prev = curr;
            for (next = req_next(curr); next != NULL; next = req_next(prev)) {
                next_key = *(uint32_t *)((uint8_t *)next + REQ_LBA_OFFSET);
                curr_key = *(uint32_t *)((uint8_t *)curr + REQ_LBA_OFFSET);

                if (next_key < curr_key) {
                    /* Swap curr and next */
                    void *tmp = req_next(curr);
                    if (prev_sorted != NULL) {
                        req_set_next(prev_sorted, next);
                    }
                    req_set_next(curr, req_next(next));
                    if (next == tmp) {
                        req_set_next(next, curr);
                    } else {
                        req_set_next(head, curr);
                        req_set_next(next, tmp);
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
        for (curr = head; curr != NULL; curr = req_next(curr)) {
            prev = curr;
            for (next = req_next(curr); next != NULL; next = req_next(prev)) {
                next_key = *(uint32_t *)((uint8_t *)next + REQ_ADDR_OFFSET);
                curr_key = *(uint32_t *)((uint8_t *)curr + REQ_ADDR_OFFSET);

                if (next_key < curr_key) {
                    /* Swap curr and next */
                    void *tmp = req_next(curr);
                    if (prev_sorted != NULL) {
                        req_set_next(prev_sorted, next);
                    }
                    req_set_next(curr, req_next(next));
                    if (next == tmp) {
                        req_set_next(next, curr);
                    } else {
                        req_set_next(head, curr);
                        req_set_next(next, tmp);
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
            next = req_next(run_start);
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
                    void *check = req_next(next);
                    while (check != NULL) {
                        int16_t check_cyl = *(int16_t *)((uint8_t *)check + REQ_CYL_OFFSET);
                        uint8_t check_head = *(uint8_t *)((uint8_t *)check + REQ_HEAD_OFFSET);
                        uint8_t check_sector = *(uint8_t *)((uint8_t *)check + REQ_SECTOR_OFFSET);

                        if (start_cyl != check_cyl ||
                            start_head != check_head) {
                            break;
                        }

                        if ((int16_t)(check_sector - start_sector) >= coalesce_limit) {
                            /*
                             * TODO(source-pxn): NOT EMITTED.  The original
                             * calls the nested procedure now named
                             * disk_$sort_swap_entries (0x00E3C370, 116
                             * bytes) here and at 0x00E3C48C / 0x00E3C560.
                             * It takes no arguments: it reaches DISK_$SORT's
                             * frame through the static link
                             * ("movea.l (A6),A0" at 0x00E3C378) and rewrites
                             * the five request-list pointers at parent
                             * A6-0x04, -0x08, -0x0C, -0x10 and -0x14,
                             * exchanging the two nodes at -0x0C and -0x10.
                             * Flattening it needs those five locals of
                             * DISK_$SORT (0x00E3C3E4, 436 bytes) identified
                             * first, so that the helper can take them by
                             * reference as a static function in this file.
                             */
                            break;
                        }

                        check = req_next(check);
                    }
                }
            }
            run_start = next;
        }
    }

    *queue_ptr = head;
}
