/*
 * dir_$old_free_slot - Free an old-format directory buffer slot
 *
 * Manages the doubly-linked list of slots in old-format directory
 * buffers. Each slot has stride 0x96 (150 bytes). Slots have:
 *   +0x36A: next link (uint16_t - slot index)
 *   +0x36C: prev link (uint16_t - slot index)
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
#define OLD_DIR_SLOT_NEXT       0x36A   /* Next link in chain (0x36A, DIR_OLD_BLOCK_NEXT) */
#define OLD_DIR_SLOT_PREV       0x36C   /* Previous link in chain (0x36C, DIR_OLD_BLOCK_PREV) */
#define OLD_DIR_SLOT_CHAIN_CNT  0x36E   /* Number of chain entries */
#define OLD_DIR_SLOT_TYPE       0x36F   /* Slot type: 0=free, 1=active */
#define OLD_DIR_FREE_HEAD       0x0C    /* Free list head in buffer header */
#define OLD_DIR_HASH_HEAD_BASE  0x3AA   /* Hash chain head words (+0x3AA) */

void dir_$old_free_slot(uint32_t handle, uint16_t hash, uint16_t slot_idx)
{
    uint32_t slot_base;
    uint16_t next;
    uint16_t prev;
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

        next = *(uint16_t *)(slot_base + OLD_DIR_SLOT_NEXT);
        prev = *(uint16_t *)(slot_base + OLD_DIR_SLOT_PREV);

        /* If next == prev, this is the only entry - clear next */
        if (next == prev) {
            next = 0;
        }

        /* Update the successor's back link */
        if (next != 0) {
            *(uint16_t *)(handle + (uint32_t)next * OLD_DIR_SLOT_STRIDE
                          + OLD_DIR_SLOT_PREV) = prev;
        }

        /* Update the predecessor, or the chain head when there is none */
        if (prev == 0) {
            /* No predecessor: the block was the chain head, so the head word takes its successor */
            *(uint16_t *)(handle + (uint32_t)hash * 2
                          + OLD_DIR_HASH_HEAD_BASE) = next;
        } else {
            *(uint16_t *)(handle + (uint32_t)prev * OLD_DIR_SLOT_STRIDE
                          + OLD_DIR_SLOT_NEXT) = next;
        }
    }

    /*
     * Prepend the slot to the free list.
     * The free list head is at handle + 0x0C.
     */
    old_free_head = *(uint16_t *)(handle + OLD_DIR_FREE_HEAD);
    *(uint16_t *)(handle + OLD_DIR_FREE_HEAD) = slot_idx;
    *(uint16_t *)(slot_base + OLD_DIR_SLOT_NEXT) = old_free_head;
    *(uint16_t *)(slot_base + OLD_DIR_SLOT_PREV) = 0;
    *(uint8_t *)(slot_base + OLD_DIR_SLOT_TYPE) = 0;
}
