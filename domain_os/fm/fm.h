/*
 * FM - File Map
 *
 * This module provides file map operations for reading and writing
 * indirect block pointer arrays. File maps are used by the file system
 * to map logical file block numbers to physical disk blocks.
 *
 * File map entries are 128 bytes (0x80) containing 32 block pointers.
 * These entries can be stored either in:
 *   - VTOCE direct block area (for level 1 / direct blocks)
 *   - Separate file map blocks (for level 2+ / indirect blocks)
 *
 * Entry size differences based on storage location:
 *   - VTOCE new format: entry at offset 0xD8 within 0x150-byte VTOCE
 *   - VTOCE old format: entry at offset 0x44 within 0xCC-byte VTOCE
 *   - File map blocks: 0x80 bytes per entry, 8 entries per 1024-byte block
 */

#ifndef FM_H
#define FM_H

#include "base/base.h"
#include "uid/uid.h"

/*
 * File map entry - 128 bytes (32 block pointers)
 *
 * Used for indirect block addressing:
 *   - Level 1: Direct blocks in VTOCE (first 8 of 32 pointers)
 *   - Level 2: Single indirect block (32 pointers to data blocks)
 *   - Level 3: Double indirect block (32 pointers to level 2 blocks)
 *   - Level 4: Triple indirect block (32 pointers to level 3 blocks)
 *
 * Block pointer value of 0 indicates unallocated block.
 */
typedef struct fm_$entry_t {
    uint32_t    blocks[32];         /* 32 block pointers (0x80 bytes total) */
} fm_$entry_t;

/*
 * File reference structure
 *
 * Contains information needed to locate a file's data on disk.
 * Passed to FM_$READ and FM_$WRITE to identify the file.
 *
 * Note: This is a partial definition based on observed usage.
 * The full structure may be larger.
 */
typedef struct fm_$file_ref_t {
    uint32_t    reserved_00;        /* 0x00: Reserved */
    uint32_t    reserved_04;        /* 0x04: Reserved */
    uid_t       file_uid;           /* 0x08: File's UID */
    uint32_t    reserved_10;        /* 0x10: Reserved */
    uint32_t    reserved_14;        /* 0x14: Reserved */
    uint32_t    reserved_18;        /* 0x18: Reserved */
    uint8_t     vol_idx;            /* 0x1C: Volume index */
    uint8_t     reserved_1d[3];     /* 0x1D: Reserved */
} fm_$file_ref_t;

/*
 * FM_$READ - Read a file map entry
 *
 * Reads a 128-byte file map entry from either a VTOCE or a file map block.
 *
 * @param file_ref      File reference structure (contains vol_idx at +0x1C, UID at +0x08)
 * @param block_addr    Block address (high 28 bits = block number, low 4 bits = entry index)
 * @param level         0 = read from VTOCE, non-zero = read from file map block at level
 * @param entry_out     Output buffer for 128-byte file map entry
 * @param status        Output status code
 *
 * When level == 0:
 *   - Reads from VTOC block, using VTOC_$UID
 *   - Entry offset depends on volume format (new=0xD8, old=0x44)
 *
 * When level != 0:
 *   - Reads from file map block, using file_ref->file_uid
 *   - Entry offset is entry_index * 0x80
 *
 * Special case: If volume is write-protected and block 1, entry 15 is requested,
 * returns a zeroed entry (unused allocation bitmap area).
 *
 * Original address: 0x00e3a314
 */
void FM_$READ(fm_$file_ref_t *file_ref, uint32_t block_addr, uint16_t level,
              fm_$entry_t *entry_out, status_$t *status);

/*
 * FM_$WRITE - Write a file map entry
 *
 * Writes a 128-byte file map entry to either a VTOCE or a file map block.
 *
 * @param file_ref      File reference structure (contains vol_idx at +0x1C, UID at +0x08)
 * @param block_addr    Block address (high 28 bits = block number, low 4 bits = entry index)
 * @param level         0 = write to VTOCE, non-zero = write to file map block at level
 * @param entry_in      Input buffer containing 128-byte file map entry to write
 * @param flags         Write flags (bit 7 set = immediate writeback)
 * @param status        Output status code
 *
 * When level == 0:
 *   - Writes to VTOC block, using VTOC_$UID
 *   - Entry offset depends on volume format (new=0xD8, old=0x44)
 *
 * When level != 0:
 *   - Writes to file map block, using file_ref->file_uid
 *   - Entry offset is entry_index * 0x80
 *
 * Buffer flags:
 *   - flags bit 7 set: 0x0B (write immediately)
 *   - flags bit 7 clear: 0x09 (dirty, write later)
 *
 * Original address: 0x00e3a45c
 */
void FM_$WRITE(fm_$file_ref_t *file_ref, uint32_t block_addr, uint16_t level,
               fm_$entry_t *entry_in, char flags, status_$t *status);

#endif /* FM_H */
