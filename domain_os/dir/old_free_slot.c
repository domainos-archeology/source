/*
 * dir_$old_free_slot - Free an old-format directory buffer slot
 *
 * Manages the doubly-linked list of slots in old-format directory
 * buffers. Each slot has stride 0x96 (150 bytes). Slots have:
 *   +0x36A: prev link (uint16_t - slot index)
 *   +0x36C: next link (uint16_t - slot index)
 *   +0x36E: chain entry count (uint8_t)
 *   +0x36F: slot type (uint8_t, 0=free, 1=active)
 *
 * If the slot is active (type==1) and has no chain entries, it is
 * first unlinked from its chain. Then the slot is prepended to
 * the free list at handle+0x0C.
 *
 * Called by dir_$old_delete_entry and dir_$old_add_link_entry.
 *
 * Parameters:
 *   handle    - Base address of directory buffer
 *   hash      - Hash bucket index (used to update tail pointer at
 *               handle + hash*2 + 0x3AA)
 *   slot_idx  - Index of slot to free
 *
 * Original address: 0x00E5518C
 * Original size: 148 bytes
 */

#include "dir/dir_internal.h"

/*
 * Old-format directory buffer slot offsets (relative to slot base)
 * Slot base = handle + slot_idx * OLD_DIR_SLOT_STRIDE
 */
#define OLD_DIR_SLOT_STRIDE     0x96    /* 150 bytes per slot */
#define OLD_DIR_SLOT_PREV       0x36A   /* Previous link in chain */
#define OLD_DIR_SLOT_NEXT       0x36C   /* Next link in chain */
#define OLD_DIR_SLOT_CHAIN_CNT  0x36E   /* Number of chain entries */
#define OLD_DIR_SLOT_TYPE       0x36F   /* Slot type: 0=free, 1=active */
#define OLD_DIR_FREE_HEAD       0x0C    /* Free list head in buffer header */
#define OLD_DIR_HASH_TAIL_BASE  0x3AA   /* Hash chain tail pointers base */

void dir_$old_free_slot(uint32_t handle, uint16_t hash, uint16_t slot_idx)
{
    uint32_t slot_base;
    uint16_t prev;
    uint16_t next;
    uint16_t old_free_head;

    slot_base = handle + (uint32_t)slot_idx * OLD_DIR_SLOT_STRIDE;

    /*
     * If slot is active (type==1), unlink it from its chain first.
     * But only if the chain entry count is zero - if entries remain,
     * just return (slot is still needed).
     */
    if (*(uint8_t *)(slot_base + OLD_DIR_SLOT_TYPE) == 1) {
        if (*(uint8_t *)(slot_base + OLD_DIR_SLOT_CHAIN_CNT) != 0) {
            return;
        }

        prev = *(uint16_t *)(slot_base + OLD_DIR_SLOT_PREV);
        next = *(uint16_t *)(slot_base + OLD_DIR_SLOT_NEXT);

        /* If prev == next, this is the only entry - clear prev */
        if (prev == next) {
            prev = 0;
        }

        /* Update prev's next pointer */
        if (prev != 0) {
            *(uint16_t *)(handle + (uint32_t)prev * OLD_DIR_SLOT_STRIDE
                          + OLD_DIR_SLOT_NEXT) = next;
        }

        /* Update next's prev pointer, or update hash tail if at end */
        if (next == 0) {
            /* This was the tail - update the hash chain tail pointer */
            *(uint16_t *)(handle + (uint32_t)hash * 2
                          + OLD_DIR_HASH_TAIL_BASE) = prev;
        } else {
            *(uint16_t *)(handle + (uint32_t)next * OLD_DIR_SLOT_STRIDE
                          + OLD_DIR_SLOT_PREV) = prev;
        }
    }

    /*
     * Prepend the slot to the free list.
     * The free list head is at handle + 0x0C.
     */
    old_free_head = *(uint16_t *)(handle + OLD_DIR_FREE_HEAD);
    *(uint16_t *)(handle + OLD_DIR_FREE_HEAD) = slot_idx;
    *(uint16_t *)(slot_base + OLD_DIR_SLOT_PREV) = old_free_head;
    *(uint16_t *)(slot_base + OLD_DIR_SLOT_NEXT) = 0;
    *(uint8_t *)(slot_base + OLD_DIR_SLOT_TYPE) = 0;
}
