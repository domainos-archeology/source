/*
 * BAT Subsystem Tests
 *
 * These tests validate the BAT (Block Allocation Table) subsystem.
* Note: Many functions require a mounted volume and disk I/O, so
 * we can only test basic data structure manipulation here.
 *
 * The kernel headers are included before any host header so that the
 * Domain/OS definitions of clock_t, uid_t, true/false, etc. win.
 */

#include "bat/bat_internal.h"

#include <stdio.h>
#include <string.h>
#include <assert.h>

/* Implementation and data under test (included directly) */
#include "../bat_data.c"
#include "../cancel.c"
#include "../get_bat_step.c"

/* Mock implementations for testing */
static int ml_lock_count = 0;
static int ml_unlock_count = 0;

void ML_$LOCK(int16_t id) {
    (void)id;
    ml_lock_count++;
}

void ML_$UNLOCK(int16_t id) {
    (void)id;
    ml_unlock_count++;
}

/*
 * Test BAT_$GET_BAT_STEP
 */
static void test_get_bat_step(void) {
    printf("Testing BAT_$GET_BAT_STEP...\n");

    /* Volume indices run 1..6 (0x00E3BA1A tst.w / 0x00E3BA1E cmpi.w #0x6) */
    memset(&bat_$volumes[1], 0, sizeof(bat_$volume_t));
    bat_$volumes[1].bat_step = 0x1234;

    memset(&bat_$volumes[2], 0, sizeof(bat_$volume_t));
    bat_$volumes[2].bat_step = 0x5678;

    /* Test retrieval */
    assert(BAT_$GET_BAT_STEP(1) == 0x1234);
    assert(BAT_$GET_BAT_STEP(2) == 0x5678);

    printf("  PASSED\n");
}

/*
 * Test BAT_$CANCEL
 */
static void test_cancel(void) {
    status_$t status;

    printf("Testing BAT_$CANCEL...\n");

    /* Initialize test data */
    memset(&bat_$volumes[1], 0, sizeof(bat_$volume_t));
    bat_$volumes[1].free_blocks = 100;
    bat_$volumes[1].reserved_blocks = 50;

    ml_lock_count = 0;
    ml_unlock_count = 0;

    /* Test successful cancel */
    BAT_$CANCEL(1, 20, &status);
    assert(status == status_$ok);
    assert(bat_$volumes[1].free_blocks == 120);
    assert(bat_$volumes[1].reserved_blocks == 30);

    /* Verify lock was taken and released */
    assert(ml_lock_count == 1);
    assert(ml_unlock_count == 1);

    /* Test cancel more than reserved (should fail) */
    BAT_$CANCEL(1, 100, &status);
    assert(status == bat_$error);
    /* Counts should not change on error */
    assert(bat_$volumes[1].free_blocks == 120);
    assert(bat_$volumes[1].reserved_blocks == 30);

    printf("  PASSED\n");
}

/*
 * Test partition VTOCE block macros
 */
static void test_vtoce_block_macros(void) {
    bat_$partition_t part;

    printf("Testing VTOCE block macros...\n");

    /* Test setting and getting VTOCE block */
    BAT_SET_VTOCE_BLOCK(&part, 0x123456);
    assert(BAT_GET_VTOCE_BLOCK(&part) == 0x123456);

    BAT_SET_VTOCE_BLOCK(&part, 0x000000);
    assert(BAT_GET_VTOCE_BLOCK(&part) == 0x000000);

    BAT_SET_VTOCE_BLOCK(&part, 0xFFFFFF);
    assert(BAT_GET_VTOCE_BLOCK(&part) == 0xFFFFFF);

    printf("  PASSED\n");
}

/*
 * Test data structure sizes
 */
static void test_structure_sizes(void) {
    printf("Testing structure sizes...\n");

    /* bat_$partition_t is 8 bytes: lsl.l #0x3 at 0x00E3AD74 */
    assert(sizeof(bat_$partition_t) == 8);

    /* bat_$volume_t is 0x234 bytes: mulu.w #0x234 at 0x00E3B756 */
    assert(sizeof(bat_$volume_t) == 0x234);

    /* The partition array runs +0x2C..+0x22B (0x00E3B810 / 0x00E3B838) */
    assert(__builtin_offsetof(bat_$volume_t, partitions) == 0x2C);
    assert(BAT_MAX_PARTITIONS == 0x40);
    assert(__builtin_offsetof(bat_$volume_t, alloc_chunk_size) == 0x22C);
    assert(__builtin_offsetof(bat_$volume_t, alloc_chunk_offset) == 0x230);

    printf("  PASSED\n");
}

int main(void) {
    printf("=== BAT Subsystem Tests ===\n\n");

    test_structure_sizes();
    test_vtoce_block_macros();
    test_get_bat_step();
    test_cancel();

    printf("\n=== All tests passed ===\n");
    return 0;
}
