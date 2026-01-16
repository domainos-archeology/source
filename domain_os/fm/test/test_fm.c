/*
 * FM Subsystem Tests
 *
 * These tests validate the FM (File Map) subsystem data structures
 * and basic functionality.
 *
 * Note: The actual FM_$READ and FM_$WRITE functions require disk
 * infrastructure (DBUF_$GET_BLOCK, etc.), so we can only test
 * basic data structure manipulation and address extraction here.
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>

/* Provide base types for testing without full kernel headers */
typedef signed char         int8_t;
typedef short               int16_t;
typedef int                 int32_t;
typedef unsigned char       uint8_t;
typedef unsigned short      uint16_t;
typedef unsigned int        uint32_t;
typedef long                status_$t;
typedef unsigned short      ushort;

typedef struct uid_t {
    uint32_t high;
    uint32_t low;
} uid_t;

/* Include FM header definitions (inline for test) */
typedef struct fm_$entry_t {
    uint32_t    blocks[32];
} fm_$entry_t;

typedef struct fm_$file_ref_t {
    uint32_t    reserved_00;
    uint32_t    reserved_04;
    uid_t       file_uid;
    uint32_t    reserved_10;
    uint32_t    reserved_14;
    uint32_t    reserved_18;
    uint8_t     vol_idx;
    uint8_t     reserved_1d[3];
} fm_$file_ref_t;

/* FM internal constants */
#define FM_ENTRY_SIZE           0x80
#define FM_ENTRIES_PER_BLOCK    8
#define FM_VTOCE_NEW_OFFSET     0xD8
#define FM_VTOCE_OLD_OFFSET     0x44
#define FM_VTOCE_NEW_SIZE       0x150
#define FM_VTOCE_OLD_SIZE       0xCC

/* Address extraction macros */
#define FM_BLOCK_NUMBER(addr)   ((addr) >> 4)
#define FM_ENTRY_INDEX(addr)    ((addr) & 0x0F)

/*
 * Note: Mock implementations for ML_$LOCK/ML_$UNLOCK and VTOC globals
 * would be needed to test FM_$READ/FM_$WRITE directly, but these functions
 * also require DBUF infrastructure which is not easily mockable.
 * The tests below focus on data structure validation and offset calculations.
 */

/*
 * Test fm_$entry_t structure size
 *
 * A file map entry must be exactly 128 bytes (32 longwords).
 */
static void test_entry_size(void) {
    printf("Testing fm_$entry_t size...\n");

    assert(sizeof(fm_$entry_t) == 128);
    assert(sizeof(fm_$entry_t) == FM_ENTRY_SIZE);

    printf("  fm_$entry_t size: %zu bytes (expected 128)\n",
           sizeof(fm_$entry_t));
    printf("  PASSED\n");
}

/*
 * Test fm_$file_ref_t structure layout
 *
 * The file reference structure must have the correct field offsets
 * as expected by FM_$READ and FM_$WRITE.
 */
static void test_file_ref_layout(void) {
    fm_$file_ref_t ref;
    char *base = (char *)&ref;

    printf("Testing fm_$file_ref_t layout...\n");

    /* Verify field offsets */
    assert((char *)&ref.file_uid - base == 0x08);
    assert((char *)&ref.vol_idx - base == 0x1C);

    printf("  file_uid offset: 0x%lx (expected 0x08)\n",
           (long)((char *)&ref.file_uid - base));
    printf("  vol_idx offset: 0x%lx (expected 0x1C)\n",
           (long)((char *)&ref.vol_idx - base));
    printf("  PASSED\n");
}

/*
 * Test address extraction macros
 *
 * Block addresses pack block number (high 28 bits) and entry index (low 4 bits).
 */
static void test_address_macros(void) {
    printf("Testing address extraction macros...\n");

    /* Test basic extraction */
    assert(FM_BLOCK_NUMBER(0x12340) == 0x1234);
    assert(FM_ENTRY_INDEX(0x12340) == 0x0);

    assert(FM_BLOCK_NUMBER(0x1234F) == 0x1234);
    assert(FM_ENTRY_INDEX(0x1234F) == 0xF);

    assert(FM_BLOCK_NUMBER(0x00010) == 0x0001);
    assert(FM_ENTRY_INDEX(0x0001F) == 0xF);

    /* Test edge cases */
    assert(FM_BLOCK_NUMBER(0x00000) == 0x0000);
    assert(FM_ENTRY_INDEX(0x00000) == 0x0);

    assert(FM_BLOCK_NUMBER(0xFFFFFFF0) == 0x0FFFFFFF);
    assert(FM_ENTRY_INDEX(0xFFFFFFF5) == 0x5);

    printf("  PASSED\n");
}

/*
 * Test level parameter to param4 conversion
 *
 * The param4 value passed to DBUF_$GET_BLOCK encodes the block range
 * threshold for the file map level.
 */
static void test_level_to_param4(void) {
    printf("Testing level to param4 conversion...\n");

    /* Formula: param4 = ((level - 1) / 8) * 256 + 32 */
    int level, level_adj;
    uint32_t param4;

    /* Level 1-8 should all map to 0x20 (32) */
    for (level = 1; level <= 8; level++) {
        level_adj = level - 1;
        param4 = (level_adj >> 3) * 0x100 + 0x20;
        assert(param4 == 0x20);
    }

    /* Level 9-16 should map to 0x120 (288) */
    for (level = 9; level <= 16; level++) {
        level_adj = level - 1;
        param4 = (level_adj >> 3) * 0x100 + 0x20;
        assert(param4 == 0x120);
    }

    /* Level 17-24 should map to 0x220 (544) */
    for (level = 17; level <= 24; level++) {
        level_adj = level - 1;
        param4 = (level_adj >> 3) * 0x100 + 0x20;
        assert(param4 == 0x220);
    }

    printf("  PASSED\n");
}

/*
 * Test VTOCE entry offset calculations
 *
 * Verify that the offset calculations for new and old format VTOCEs
 * are correct.
 */
static void test_vtoce_entry_offsets(void) {
    printf("Testing VTOCE entry offset calculations...\n");

    /* New format: entry at index * 0x150 + 0xD8 */
    for (int idx = 0; idx < 4; idx++) {
        uint32_t offset = idx * FM_VTOCE_NEW_SIZE + FM_VTOCE_NEW_OFFSET;
        printf("  New format entry %d at offset 0x%x\n", idx, offset);
        assert(offset == (uint32_t)(idx * 0x150 + 0xD8));
    }

    /* Old format: entry at index * 0xCC + 0x44 */
    for (int idx = 0; idx < 5; idx++) {
        uint32_t offset = idx * FM_VTOCE_OLD_SIZE + FM_VTOCE_OLD_OFFSET;
        printf("  Old format entry %d at offset 0x%x\n", idx, offset);
        assert(offset == (uint32_t)(idx * 0xCC + 0x44));
    }

    printf("  PASSED\n");
}

/*
 * Test file map block entry offsets
 *
 * File map blocks have 8 entries of 0x80 bytes each.
 */
static void test_fm_block_offsets(void) {
    printf("Testing file map block entry offsets...\n");

    for (int idx = 0; idx < FM_ENTRIES_PER_BLOCK; idx++) {
        uint32_t offset = idx * FM_ENTRY_SIZE;
        printf("  FM block entry %d at offset 0x%x\n", idx, offset);
        assert(offset == (uint32_t)(idx * 0x80));
    }

    /* Verify all 8 entries fit in a 1024-byte block */
    assert(FM_ENTRIES_PER_BLOCK * FM_ENTRY_SIZE == 1024);

    printf("  PASSED\n");
}

/*
 * Test file map entry initialization
 */
static void test_entry_initialization(void) {
    fm_$entry_t entry;

    printf("Testing file map entry initialization...\n");

    /* Zero initialize */
    memset(&entry, 0, sizeof(entry));
    for (int i = 0; i < 32; i++) {
        assert(entry.blocks[i] == 0);
    }

    /* Set some values */
    entry.blocks[0] = 0x1000;
    entry.blocks[7] = 0x7000;
    entry.blocks[31] = 0xFFFFFFFF;

    assert(entry.blocks[0] == 0x1000);
    assert(entry.blocks[7] == 0x7000);
    assert(entry.blocks[31] == 0xFFFFFFFF);
    /* Unset entries should still be zero */
    assert(entry.blocks[1] == 0);
    assert(entry.blocks[30] == 0);

    printf("  PASSED\n");
}

int main(void) {
    printf("=== FM Subsystem Tests ===\n\n");

    test_entry_size();
    test_file_ref_layout();
    test_address_macros();
    test_level_to_param4();
    test_vtoce_entry_offsets();
    test_fm_block_offsets();
    test_entry_initialization();

    printf("\n=== All tests passed ===\n");
    return 0;
}
