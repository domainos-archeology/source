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
 *   - Link data refs: the 8-byte dir_$old_link_refs_t at +0x12, whose
 *     block1 (+0x14) and block2 (+0x16) words are the two overflow slots
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
#include "arch/arch.h"

void dir_$old_delete_entry(uint32_t handle, uint16_t slot_idx,
                           uint16_t chain_level, uint16_t hash)
{
    char *base = (char *)ARCH_VA_TO_PTR(handle);
    uint8_t entry_type;                 /* D3b */
    /*
     * A6-0x10: the eight bytes the image copies out of the entry with two
     * longword moves before it clears the slot.  Only the two middle words
     * are read back, at A6-0x0E and A6-0x0C.
     */
    dir_$old_link_refs_t refs;

    if (chain_level == 0) {
        /* 0x00E555F2-0x00E5561E: inline entry at slot_idx * 0x30 */
        char *entry = base + (uint32_t)slot_idx * 0x30;

        /* 0x00E55604 */
        entry_type = *(uint8_t *)(entry + 0x11);

        /* 0x00E55608-0x00E55610: lea (0x12,A0),A1 then two move.l (A1)+ */
        refs = *(const dir_$old_link_refs_t *)(entry + 0x12);

        /* 0x00E55614: clear the type byte (mark the slot empty) */
        *(uint8_t *)(entry + 0x11) = 0;
        /* 0x00E55618: bclr #7 of the flag byte at +0x0E */
        *(uint8_t *)(entry + 0x0e) &= 0x7F;
    } else {
        /* 0x00E55620-0x00E55666: overflow bucket + chain offset */
        char *bucket = base + (uint32_t)slot_idx * 0x96;
        char *entry = bucket + (uint32_t)chain_level * 0x30;

        /* 0x00E5563C */
        entry_type = *(uint8_t *)(entry + 0x367);

        /* 0x00E55640-0x00E55648: lea (0x368,A1),A3 then two move.l (A3)+ */
        refs = *(const dir_$old_link_refs_t *)(entry + 0x368);

        /* 0x00E5564C / 0x00E55650 */
        *(uint8_t *)(entry + 0x367) = 0;
        *(uint8_t *)(entry + 0x364) &= 0x7F;
        /* 0x00E55656: subq.b #1 on the bucket's chain count */
        *(uint8_t *)(bucket + 0x36e) -= 1;
        /* 0x00E5565A-0x00E55666 */
        dir_$old_free_slot(handle, hash, slot_idx);
    }

    /* 0x00E55668: subq.w #1,(0x16,A2) */
    *(int16_t *)(base + 0x16) -= 1;

    /*
     * 0x00E5566C-0x00E5568E: a soft link (type 3) also owns up to two
     * overflow slots, named by the two WORDS of the saved block.
     */
    if (entry_type == 3) {
        dir_$old_free_slot(handle, 0, refs.block1);   /* 0x00E55672 */
        if (refs.block2 != 0) {                       /* 0x00E55680 tst.w */
            dir_$old_free_slot(handle, 0, refs.block2);
        }
    }
}
