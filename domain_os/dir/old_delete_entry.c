/*
 * dir_$old_delete_entry - Delete/clear a directory entry
 *
 * Removes an entry from the directory buffer. Handles both inline
 * entries (stride 0x30, chain_level=0) and overflow entries
 * (bucket stride 0x96, chain_level>0). Clears the entry type and
 * active flag, decrements the total entry count, and frees any
 * overflow link data blocks (for type 3/soft link entries).
 *
 * For inline entries (chain_level == 0):
 *   Entry at handle + slot_idx * 0x30
 *   - Type byte at +0x11, flag byte at +0x0e (bit 7 = active)
 *   - Link data refs at +0x14 (low16 of uint32@+0x12) and
 *     +0x16 (high16 of uint32@+0x16)
 *
 * For overflow entries (chain_level != 0):
 *   Bucket at handle + slot_idx * 0x96
 *   Entry within at bucket + chain_level * 0x30
 *   - Type byte at +0x367, flag byte at +0x364 (bit 7 = active)
 *   - Link data refs at +0x36a and +0x36c
 *   - Chain count at bucket + 0x36e (decremented)
 *
 * Parameters:
 *   handle      - Base of mapped directory buffer
 *   slot_idx    - Inline entry index or overflow bucket index
 *   chain_level - 0 for inline, >0 for overflow chain position
 *   hash        - Hash value for overflow chain cleanup
 *
 * Original address: 0x00E555DC
 * Size: 192 bytes
 */

#include "dir/dir_internal.h"

void dir_$old_delete_entry(uint32_t handle, uint16_t slot_idx,
                           uint16_t chain_level, uint16_t hash)
{
    char *base = (char *)(uintptr_t)handle;
    uint8_t entry_type;
    uint16_t link_block1, link_block2;

    if (chain_level == 0) {
        /* Inline entry at slot_idx * 0x30 */
        char *entry = base + (uint32_t)slot_idx * 0x30;

        /* Save entry type and link data references */
        entry_type = *(uint8_t *)(entry + 0x11);
        /* Low 16 bits of uint32 at +0x12 (big-endian: bytes at +0x14..+0x15) */
        link_block1 = (uint16_t)(*(uint32_t *)(entry + 0x12) & 0xFFFF);
        /* High 16 bits of uint32 at +0x16 (big-endian: bytes at +0x16..+0x17) */
        link_block2 = (uint16_t)(*(uint32_t *)(entry + 0x16) >> 16);

        /* Clear entry type (mark empty) */
        *(uint8_t *)(entry + 0x11) = 0;
        /* Clear active bit (bit 7 of flag byte at +0x0e) */
        *(uint8_t *)(entry + 0x0e) &= 0x7F;
    } else {
        /* Overflow entry: bucket base + chain offset */
        char *bucket = base + (uint32_t)slot_idx * 0x96;
        char *entry = bucket + (uint32_t)chain_level * 0x30;

        /* Save entry type and link data references */
        entry_type = *(uint8_t *)(entry + 0x367);
        link_block1 = (uint16_t)(*(uint32_t *)(entry + 0x368) & 0xFFFF);
        link_block2 = (uint16_t)(*(uint32_t *)(entry + 0x36c) >> 16);

        /* Clear entry type */
        *(uint8_t *)(entry + 0x367) = 0;
        /* Clear active bit */
        *(uint8_t *)(entry + 0x364) &= 0x7F;
        /* Decrement chain entry count */
        *(uint8_t *)(bucket + 0x36e) -= 1;
        /* Release/cleanup the overflow slot */
        FUN_00e5518c(handle, hash, slot_idx);
    }

    /* Decrement total entry count at handle + 0x16 */
    *(int16_t *)(base + 0x16) -= 1;

    /* If entry was a link (type 3), free associated data blocks */
    if (entry_type == 3) {
        FUN_00e5518c(handle, 0, link_block1);
        if (link_block2 != 0) {
            FUN_00e5518c(handle, 0, link_block2);
        }
    }
}
