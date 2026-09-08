/*
 * dir_$old_init_buf - Initialize old-format directory buffer
 *
 * Initializes a directory buffer structure for a new or reinitializing
 * directory. Sets up the header fields, clears entry arrays, hash table,
 * and info block area.
 *
 * The old-format directory buffer layout (total ~0x400 bytes):
 *   0x00-0x01: uint16_t version (1)
 *   0x02-0x03: uint16_t num_hash_buckets (43 = 0x2B)
 *   0x04-0x05: uint16_t max_inline_entries (18 = 0x12)
 *   0x06-0x07: uint16_t field_06 (0x01AD = 429)
 *   0x08-0x09: uint16_t field_08 (3)
 *   0x0A-0x0D: uint32_t entry_count (initialized to 0)
 *   0x0E-0x15: uid_t    parent_uid (initialized to UID_$NIL)
 *   0x16-0x19: uint32_t capacity (0x514 = 1300)
 *   0x1A-0x2F: (gap)
 *   0x30-0x38F: Inline entry area (18 entries, stride 0x30)
 *     - Each entry has an "active" flag at offset 0x11 within it
 *   0x37A:     uint8_t  initialized_flag (set to 1)
 *   0x37B:     uint8_t  reserved_37B (cleared)
 *   0x37C-0x37D: int16_t info_write_len (cleared)
 *   0x37E-0x37F: int16_t info_read_len (cleared)
 *   0x380-0x381: uint16_t field_380 (cleared)
 *   0x382-0x3A9: Info block data area (40 bytes, cleared)
 *   0x3AA-0x3FF: Hash table (43 entries, 2 bytes each, cleared)
 *
 * Parameters:
 *   buffer - Pointer to directory buffer to initialize
 *
 * Original address: 0x00E544B0
 * Size: 140 bytes
 */

#include "dir/dir_internal.h"

/*
 * Header template constants (from 0x00E5453C, 10 bytes at 0x00E5453C):
 *   00 01 00 2b 00 12 01 ad 00 03
 */
#define DIR_OLD_BUF_VERSION         1
#define DIR_OLD_NUM_HASH_BUCKETS    43      /* 0x2B */
#define DIR_OLD_MAX_INLINE_ENTRIES  18      /* 0x12 */
#define DIR_OLD_FIELD_06            0x01AD  /* 429 */
#define DIR_OLD_FIELD_08            3

/* Stride between inline entry slots */
#define DIR_OLD_ENTRY_STRIDE        0x30

/* Offset of "active" flag byte within each inline entry slot */
#define DIR_OLD_ENTRY_ACTIVE_OFFSET 0x11

/* Maximum info block data length (Pascal 1-indexed array) */
#define DIR_OLD_MAX_INFO_LEN        40

void dir_$old_init_buf(void *buffer)
{
    uint8_t *buf = (uint8_t *)buffer;
    int16_t i;

    /*
     * Step 1: Write header fields (equivalent to 10-byte template copy
     * from 0x00E5453C at offsets 0x00-0x09)
     * Assembly: lea (0x7e,PC),A1; move.l/move.l/move.w
     */
    *(uint16_t *)(buf + 0x00) = DIR_OLD_BUF_VERSION;
    *(uint16_t *)(buf + 0x02) = DIR_OLD_NUM_HASH_BUCKETS;
    *(uint16_t *)(buf + 0x04) = DIR_OLD_MAX_INLINE_ENTRIES;
    *(uint16_t *)(buf + 0x06) = DIR_OLD_FIELD_06;
    *(uint16_t *)(buf + 0x08) = DIR_OLD_FIELD_08;

    /*
     * Step 2: Set parent UID to UID_$NIL at offsets 0x0E and 0x12
     * Assembly: movea.l #0xe1737c,A1; move.l (A1)+,(0xe,A0); move.l (A1)+,(0x12,A0)
     */
    *(uint32_t *)(buf + 0x0E) = UID_$NIL.high;
    *(uint32_t *)(buf + 0x12) = UID_$NIL.low;

    /*
     * Step 3: Set capacity to 0x514 at offset 0x16
     * Assembly: move.l #0x514,(0x16,A0)
     */
    *(uint32_t *)(buf + 0x16) = 0x514;

    /*
     * Step 4: Clear entry count at offset 0x0A
     * Assembly: clr.l (0xa,A0)
     */
    *(uint32_t *)(buf + 0x0A) = 0;

    /*
     * Step 5: Clear hash table (43 two-byte entries at offset 0x3AA)
     * Assembly: moveq #0x2a,D0; loop clr.w (0x3aa,A1)
     */
    for (i = 0; i < DIR_OLD_NUM_HASH_BUCKETS; i++) {
        *(uint16_t *)(buf + 0x3AA + i * 2) = 0;
    }

    /*
     * Step 6: Clear "active" flag byte at offset 0x11 within each of
     * 18 inline entry slots (stride 0x30, starting at buf+0x30)
     * Assembly: moveq #0x11,D0; lea (0x30,A0),A1; loop clr.b (0x11,A1); lea (0x30,A1),A1
     */
    for (i = 0; i < DIR_OLD_MAX_INLINE_ENTRIES; i++) {
        *(buf + 0x30 + i * DIR_OLD_ENTRY_STRIDE + DIR_OLD_ENTRY_ACTIVE_OFFSET) = 0;
    }

    /*
     * Step 7: Set initialized flag, clear reserved byte
     * Assembly: move.b #0x1,(0x37a,A0); clr.b (0x37b,A0)
     */
    *(buf + 0x37A) = 1;
    *(buf + 0x37B) = 0;

    /*
     * Step 8: Clear info block length fields and field_380
     * Assembly: clr.l (0x37c,A0); clr.w (0x380,A0)
     */
    *(uint32_t *)(buf + 0x37C) = 0;
    *(uint16_t *)(buf + 0x380) = 0;

    /*
     * Step 9: Clear info block data area (40 bytes at 0x382-0x3A9)
     * The original Pascal array is 1-indexed: data[1] at 0x382, data[40] at 0x3A9
     * Assembly: moveq #0x27,D1; moveq #0x1,D0; loop clr.b (0x381,A2) with D0 as index
     */
    for (i = 1; i <= DIR_OLD_MAX_INFO_LEN; i++) {
        *(buf + 0x381 + i) = 0;
    }
}
