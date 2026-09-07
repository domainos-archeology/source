/*
 * smd/test/test_hdm.c - Unit tests for SMD_$ALLOC_HDM (0x00E6D92E),
 * SMD_$FREE_HDM (0x00E6DA3A) and smd_$reset_unit_display (0x00E6D736).
 *
 * All three real .c files are #included below and the real functions are
 * called; everything they depend on is mocked in this translation unit.
 *
 * Facts under test:
 *   - the free-list block array starts at list+0x02, not list+0x04:
 *     SMD_$ALLOC_HDM reads block k-1's size at list+4+4*(k-1) and its offset
 *     two bytes earlier (0x00E6D98A "lea (0x4,A2),A0", 0x00E6D992
 *     "cmp.w (A1),D1w", 0x00E6D9B4 "move.w (-0x2,A1),(A4)")
 *   - an HDM position is stored row first: 0x00E6D9AE writes the constant 800
 *     to +0x02 and 0x00E6D9B4 the scan line to +0x00
 *   - the free list lives at the unit record's +0xF8 field (0x00E6D974
 *     "movea.l (0x4,A0),A2")
 *   - smd_$reset_unit_display seeds that list with exactly one block whose
 *     {offset,size} pair is the longword written at list+0x02
 *     (0x00E6D788 / 0x00E6D792), and clears all eight font pointers
 *     (0x00E6D76E)
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "smd/smd_internal.h"

/* ------------------------------------------------------------------ */
/* Test harness                                                        */
/* ------------------------------------------------------------------ */

static int tests_passed = 0;
static int tests_failed = 0;
static int current_failed = 0;

#define RUN_TEST(name)                                                        \
    do {                                                                      \
        printf("  %-44s", #name);                                             \
        current_failed = 0;                                                   \
        test_##name();                                                        \
        if (current_failed) {                                                 \
            tests_failed++;                                                   \
        } else {                                                              \
            tests_passed++;                                                   \
            printf("PASSED\n");                                               \
        }                                                                     \
    } while (0)

#define CHECK_EQ(expected, actual)                                            \
    do {                                                                      \
        long _e = (long)(expected);                                           \
        long _a = (long)(actual);                                             \
        if (_e != _a) {                                                       \
            if (!current_failed) printf("FAILED\n");                          \
            current_failed = 1;                                               \
            printf("      %s:%d: %s: expected 0x%lx, got 0x%lx\n", __FILE__,  \
                   __LINE__, #actual, (unsigned long)_e, (unsigned long)_a);  \
        }                                                                     \
    } while (0)

/* The block array must abut the count with no padding in between; this is
 * the layout fix the tests below depend on. */
_Static_assert(offsetof(smd_hdm_list_t, blocks) == 0x02, "hdm blocks at +2");
_Static_assert(offsetof(smd_hdm_pos_t, y) == 0x00, "hdm pos row first");
_Static_assert(offsetof(smd_hdm_pos_t, x) == 0x02, "hdm pos column second");
/* sizeof(smd_font_entry_t) is 8 only where a pointer is 4 bytes; the header
 * asserts that under ARCH_M68K.  The tests below index the table through the
 * struct, so the host's larger stride is harmless. */

/* ------------------------------------------------------------------ */
/* Mocked globals                                                      */
/* ------------------------------------------------------------------ */

smd_globals_t SMD_GLOBALS;
uint8_t SMD_DISPLAY_UNITS[SMD_MAX_DISPLAY_UNITS * SMD_DISPLAY_UNIT_SIZE + 0x18];
smd_display_info_t SMD_DISPLAY_INFO[SMD_MAX_DISPLAY_UNITS];
uint16_t PROC1_$AS_ID;

#define TEST_ASID 2
#define TEST_UNIT 1
#define MAX_BLOCKS 25

static smd_display_hw_t test_hw;
static struct {
    uint16_t count;
    smd_hdm_block_t blocks[MAX_BLOCKS];
} test_list;
static smd_font_entry_t test_fonts[SMD_MAX_FONTS_PER_UNIT];
static uint16_t test_display_mem[0x20010];

/* ------------------------------------------------------------------ */
/* Functions under test                                                */
/* ------------------------------------------------------------------ */

#include "../alloc_hdm.c"
#include "../free_hdm.c"
#include "../reset_unit_display.c"

static smd_display_unit_t *rec(void) { return smd_$unit_rec(TEST_UNIT); }

static void setup(uint16_t display_type)
{
    memset(&SMD_GLOBALS, 0, sizeof(SMD_GLOBALS));
    memset(SMD_DISPLAY_UNITS, 0, sizeof(SMD_DISPLAY_UNITS));
    memset(&test_hw, 0, sizeof(test_hw));
    memset(&test_list, 0, sizeof(test_list));
    memset(test_fonts, 0, sizeof(test_fonts));

    PROC1_$AS_ID = TEST_ASID;
    SMD_GLOBALS.asid_to_unit[TEST_ASID] = TEST_UNIT;
    test_hw.display_type = display_type;
    rec()->hw = &test_hw;
    rec()->hdm_list = (smd_hdm_list_t *)&test_list;
    rec()->font_table = test_fonts;
    rec()->display_base = (uint32_t)(uintptr_t)test_display_mem;
}

/* ------------------------------------------------------------------ */
/* SMD_$ALLOC_HDM                                                      */
/* ------------------------------------------------------------------ */

/* 0x00E6D956: no unit bound to this ASID. */
static void test_alloc_without_a_display(void)
{
    uint16_t size = 4;
    smd_hdm_pos_t pos = { 0, 0 };
    status_$t st = -1;

    setup(SMD_DISP_TYPE_MONO_PORTRAIT);
    SMD_GLOBALS.asid_to_unit[TEST_ASID] = 0;

    SMD_$ALLOC_HDM(&size, &pos, &st);

    CHECK_EQ(status_$display_invalid_use_of_driver_procedure, st);
}

/* 0x00E6D982 / 0x00E6DA2A: an empty list is "hidden display memory full". */
static void test_alloc_from_an_empty_list(void)
{
    uint16_t size = 4;
    smd_hdm_pos_t pos = { 0, 0 };
    status_$t st = -1;

    setup(SMD_DISP_TYPE_MONO_PORTRAIT);
    test_list.count = 0;

    SMD_$ALLOC_HDM(&size, &pos, &st);

    CHECK_EQ(status_$display_hidden_display_memory_full, st);
}

/* 0x00E6DA14: a partial allocation shrinks the block from the front. */
static void test_alloc_splits_a_block(void)
{
    uint16_t size = 0x10;
    smd_hdm_pos_t pos = { 0, 0 };
    status_$t st = -1;

    setup(SMD_DISP_TYPE_MONO_PORTRAIT);
    test_list.count = 1;
    test_list.blocks[0].offset = 0x31;
    test_list.blocks[0].size = 0x3CE;

    SMD_$ALLOC_HDM(&size, &pos, &st);

    CHECK_EQ(status_$ok, st);
    /* Display type 1 puts the column at 800 and the row at the block offset */
    CHECK_EQ(800, pos.x);
    CHECK_EQ(0x31, pos.y);
    CHECK_EQ(1, test_list.count);
    CHECK_EQ(0x41, test_list.blocks[0].offset);
    CHECK_EQ(0x3BE, test_list.blocks[0].size);
}

/* 0x00E6D9E8-0x00E6DA0E: an exact fit removes the block and compacts. */
static void test_alloc_exact_fit_removes_the_block(void)
{
    uint16_t size = 0x20;
    smd_hdm_pos_t pos = { 0, 0 };
    status_$t st = -1;

    setup(SMD_DISP_TYPE_MONO_PORTRAIT);
    test_list.count = 3;
    test_list.blocks[0].offset = 0x40;
    test_list.blocks[0].size = 0x10;   /* too small */
    test_list.blocks[1].offset = 0x80;
    test_list.blocks[1].size = 0x20;   /* exact */
    test_list.blocks[2].offset = 0x200;
    test_list.blocks[2].size = 0x100;

    SMD_$ALLOC_HDM(&size, &pos, &st);

    CHECK_EQ(status_$ok, st);
    CHECK_EQ(0x80, pos.y);
    CHECK_EQ(2, test_list.count);
    CHECK_EQ(0x40, test_list.blocks[0].offset);
    CHECK_EQ(0x200, test_list.blocks[1].offset);
    CHECK_EQ(0x100, test_list.blocks[1].size);
}

/* 0x00E6D9BA-0x00E6D9E2: display type 2 tiles the 224-line hidden band. */
static void test_alloc_landscape_position(void)
{
    uint16_t size = 1;
    smd_hdm_pos_t pos = { 0, 0 };
    status_$t st = -1;

    setup(SMD_DISP_TYPE_MONO_LANDSCAPE);
    test_list.count = 1;
    test_list.blocks[0].offset = 0xE5;   /* 229 = 1*224 + 5 */
    test_list.blocks[0].size = 0x10;

    SMD_$ALLOC_HDM(&size, &pos, &st);

    CHECK_EQ(status_$ok, st);
    CHECK_EQ(0xE0, pos.x);         /* (229 / 224) * 224 */
    CHECK_EQ(800 + 5, pos.y);      /* (229 % 224) + 800 */
}

/* ------------------------------------------------------------------ */
/* SMD_$FREE_HDM                                                       */
/* ------------------------------------------------------------------ */

/* 0x00E6DA98-0x00E6DAAC: display type 1 accepts rows 0x31..0x3FF at
 * column 800 and nothing else. */
static void test_free_validates_the_position(void)
{
    uint16_t size = 4;
    status_$t st;
    smd_hdm_pos_t bad_column = { 0x40, 799 };
    smd_hdm_pos_t low_row = { 0x30, 800 };
    smd_hdm_pos_t high_row = { 0x400, 800 };

    setup(SMD_DISP_TYPE_MONO_PORTRAIT);
    test_list.count = 0;

    st = -1;
    SMD_$FREE_HDM(&size, &bad_column, &st);
    CHECK_EQ(status_$display_invalid_position_argument, st);

    st = -1;
    SMD_$FREE_HDM(&size, &low_row, &st);
    CHECK_EQ(status_$display_invalid_position_argument, st);

    st = -1;
    SMD_$FREE_HDM(&size, &high_row, &st);
    CHECK_EQ(status_$display_invalid_position_argument, st);
}

/* 0x00E6DBB6-0x00E6DBFE: a block that touches neither neighbour is inserted
 * in offset order. */
static void test_free_inserts_in_order(void)
{
    uint16_t size = 0x10;
    smd_hdm_pos_t pos = { 0x100, 800 };
    status_$t st = -1;

    setup(SMD_DISP_TYPE_MONO_PORTRAIT);
    test_list.count = 2;
    test_list.blocks[0].offset = 0x40;
    test_list.blocks[0].size = 0x10;
    test_list.blocks[1].offset = 0x200;
    test_list.blocks[1].size = 0x10;

    SMD_$FREE_HDM(&size, &pos, &st);

    CHECK_EQ(status_$ok, st);
    CHECK_EQ(3, test_list.count);
    CHECK_EQ(0x40, test_list.blocks[0].offset);
    CHECK_EQ(0x100, test_list.blocks[1].offset);
    CHECK_EQ(0x10, test_list.blocks[1].size);
    CHECK_EQ(0x200, test_list.blocks[2].offset);
}

/* 0x00E6DB88: a block that abuts the previous one just extends it. */
static void test_free_merges_with_the_previous_block(void)
{
    uint16_t size = 0x10;
    smd_hdm_pos_t pos = { 0x50, 800 };
    status_$t st = -1;

    setup(SMD_DISP_TYPE_MONO_PORTRAIT);
    test_list.count = 1;
    test_list.blocks[0].offset = 0x40;
    test_list.blocks[0].size = 0x10;   /* ends at 0x50 */

    SMD_$FREE_HDM(&size, &pos, &st);

    CHECK_EQ(status_$ok, st);
    CHECK_EQ(1, test_list.count);
    CHECK_EQ(0x40, test_list.blocks[0].offset);
    CHECK_EQ(0x20, test_list.blocks[0].size);
}

/* 0x00E6DB96-0x00E6DBAA: a block that abuts the next one moves its start. */
static void test_free_merges_with_the_next_block(void)
{
    uint16_t size = 0x10;
    smd_hdm_pos_t pos = { 0x40, 800 };
    status_$t st = -1;

    setup(SMD_DISP_TYPE_MONO_PORTRAIT);
    test_list.count = 1;
    test_list.blocks[0].offset = 0x50;
    test_list.blocks[0].size = 0x10;

    SMD_$FREE_HDM(&size, &pos, &st);

    CHECK_EQ(status_$ok, st);
    CHECK_EQ(1, test_list.count);
    CHECK_EQ(0x40, test_list.blocks[0].offset);
    CHECK_EQ(0x20, test_list.blocks[0].size);
}

/* 0x00E6DB44-0x00E6DB82: a block that fills a hole exactly merges both
 * neighbours into one and drops an entry. */
static void test_free_merges_both_neighbours(void)
{
    uint16_t size = 0x10;
    smd_hdm_pos_t pos = { 0x50, 800 };
    status_$t st = -1;

    setup(SMD_DISP_TYPE_MONO_PORTRAIT);
    test_list.count = 2;
    test_list.blocks[0].offset = 0x40;
    test_list.blocks[0].size = 0x10;   /* ends at 0x50 */
    test_list.blocks[1].offset = 0x60;
    test_list.blocks[1].size = 0x10;

    SMD_$FREE_HDM(&size, &pos, &st);

    CHECK_EQ(status_$ok, st);
    CHECK_EQ(1, test_list.count);
    CHECK_EQ(0x40, test_list.blocks[0].offset);
    CHECK_EQ(0x30, test_list.blocks[0].size);
}

/* An allocation followed by the matching free must restore the list. */
static void test_alloc_then_free_round_trips(void)
{
    uint16_t size = 0x10;
    smd_hdm_pos_t pos = { 0, 0 };
    status_$t st = -1;

    setup(SMD_DISP_TYPE_MONO_PORTRAIT);
    test_list.count = 1;
    test_list.blocks[0].offset = 0x31;
    test_list.blocks[0].size = 0x3CE;

    SMD_$ALLOC_HDM(&size, &pos, &st);
    CHECK_EQ(status_$ok, st);

    st = -1;
    SMD_$FREE_HDM(&size, &pos, &st);
    CHECK_EQ(status_$ok, st);

    CHECK_EQ(1, test_list.count);
    CHECK_EQ(0x31, test_list.blocks[0].offset);
    CHECK_EQ(0x3CE, test_list.blocks[0].size);
}

/* ------------------------------------------------------------------ */
/* smd_$reset_unit_display                                             */
/* ------------------------------------------------------------------ */

/* 0x00E6D75E / 0x00E6D762: the clip window is reset to the screen bounds,
 * and 0x00E6D76E clears all eight font pointers. */
static void test_reset_clears_the_clip_window_and_fonts(void)
{
    int i;
    static void *dummy;

    setup(SMD_DISP_TYPE_MONO_PORTRAIT);
    test_hw.min_x = 0;
    test_hw.max_x = 0x31F;
    test_hw.min_y = 0;
    test_hw.max_y = 0x3FF;
    test_hw.clip_x1 = 11;
    test_hw.clip_x2 = 22;
    test_hw.clip_y1 = 33;
    test_hw.clip_y2 = 44;
    test_hw.tracking_enabled = 0;
    for (i = 0; i < SMD_MAX_FONTS_PER_UNIT; i++) {
        test_fonts[i].font_ptr = &dummy;
        test_fonts[i].hdm_pos.y = (uint16_t)(i + 1);
    }
    rec()->owner_asid = 0;
    rec()->borrowed_asid = 0;

    smd_$reset_unit_display(TEST_UNIT, 0); /* `full` false: no memory clear */

    CHECK_EQ(0, test_hw.clip_x1);
    CHECK_EQ(0x31F, test_hw.clip_x2);
    CHECK_EQ(0, test_hw.clip_y1);
    CHECK_EQ(0x3FF, test_hw.clip_y2);
    for (i = 0; i < SMD_MAX_FONTS_PER_UNIT; i++) {
        CHECK_EQ(0, (long)(intptr_t)test_fonts[i].font_ptr);
        /* the hdm position half of the entry is deliberately left alone */
        CHECK_EQ(i + 1, test_fonts[i].hdm_pos.y);
    }
}

/* 0x00E6D788 / 0x00E6D792: display types 2 and 6 get {0, 0x357}, everything
 * else {0x31, 0x3CE}. */
static void test_reset_seeds_the_free_list(void)
{
    setup(SMD_DISP_TYPE_MONO_PORTRAIT); /* type 1 */
    smd_$reset_unit_display(TEST_UNIT, 0);
    CHECK_EQ(1, test_list.count);
    CHECK_EQ(0x31, test_list.blocks[0].offset);
    CHECK_EQ(0x3CE, test_list.blocks[0].size);

    setup(SMD_DISP_TYPE_MONO_LANDSCAPE); /* type 2 */
    smd_$reset_unit_display(TEST_UNIT, 0);
    CHECK_EQ(1, test_list.count);
    CHECK_EQ(0x0000, test_list.blocks[0].offset);
    CHECK_EQ(0x0357, test_list.blocks[0].size);

    setup(SMD_DISP_TYPE_MONO_1024x1024_A); /* type 6 */
    smd_$reset_unit_display(TEST_UNIT, 0);
    CHECK_EQ(0x0000, test_list.blocks[0].offset);
    CHECK_EQ(0x0357, test_list.blocks[0].size);

    setup(SMD_DISP_TYPE_HI_RES_2048x1024); /* type 5 */
    smd_$reset_unit_display(TEST_UNIT, 0);
    CHECK_EQ(0x0031, test_list.blocks[0].offset);
    CHECK_EQ(0x03CE, test_list.blocks[0].size);
}

/* The seed the reset writes is exactly the range SMD_$FREE_HDM validates for
 * display type 1, so a whole-range allocation must succeed afterwards. */
static void test_reset_seed_matches_the_free_hdm_bounds(void)
{
    uint16_t size = 0x3CE;
    smd_hdm_pos_t pos = { 0, 0 };
    status_$t st = -1;

    setup(SMD_DISP_TYPE_MONO_PORTRAIT);
    smd_$reset_unit_display(TEST_UNIT, 0);

    SMD_$ALLOC_HDM(&size, &pos, &st);

    CHECK_EQ(status_$ok, st);
    CHECK_EQ(0x31, pos.y);
    CHECK_EQ(800, pos.x);
    CHECK_EQ(0, test_list.count);
}

/* 0x00E6D79A-0x00E6D7D4: the display-memory clear only runs when the unit is
 * borrowed or unowned AND the boolean argument is true.
 *
 * The record's display base is a 32-bit field, so the part of this test that
 * actually lets the routine write to display memory can only run when the
 * host puts the buffer below 4GB.  The two paths that must *not* write are
 * checked unconditionally.
 */
static void test_reset_display_memory_clear_is_conditional(void)
{
    int i;
    int can_write = (uintptr_t)test_display_mem <= 0xFFFFFFFFu;

    /* Owned and not borrowed: no clear, whatever `full` says. */
    setup(SMD_DISP_TYPE_MONO_PORTRAIT);
    memset(test_display_mem, 0x5A, sizeof(test_display_mem));
    rec()->owner_asid = 7;
    rec()->borrowed_asid = 0;

    smd_$reset_unit_display(TEST_UNIT, (boolean)0xFF);

    for (i = 0; i < 8; i++) {
        CHECK_EQ(0x5A5A, test_display_mem[0x1FFF0 / 2 + i]);
    }

    /* Unowned but `full` false: still no clear. */
    setup(SMD_DISP_TYPE_MONO_PORTRAIT);
    memset(test_display_mem, 0x5A, sizeof(test_display_mem));
    rec()->owner_asid = 0;
    rec()->borrowed_asid = 0;

    smd_$reset_unit_display(TEST_UNIT, 0);

    for (i = 0; i < 8; i++) {
        CHECK_EQ(0x5A5A, test_display_mem[0x1FFF0 / 2 + i]);
    }

    if (!can_write) {
        printf("(clear skipped: display base > 4GB) ");
        return;
    }

    /* Unowned and `full` true: four words cleared at +0x1FFF0 and four set
     * to 0xFFFF at +0x1FFF8, and nothing else. */
    setup(SMD_DISP_TYPE_MONO_PORTRAIT);
    memset(test_display_mem, 0x5A, sizeof(test_display_mem));
    rec()->owner_asid = 0;
    rec()->borrowed_asid = 0;

    smd_$reset_unit_display(TEST_UNIT, (boolean)0xFF);

    for (i = 0; i < 4; i++) {
        CHECK_EQ(0x0000, test_display_mem[0x1FFF0 / 2 + i]);
        CHECK_EQ(0xFFFF, test_display_mem[0x1FFF8 / 2 + i]);
    }
    CHECK_EQ(0x5A5A, test_display_mem[0x1FFF0 / 2 - 1]);
    CHECK_EQ(0x5A5A, test_display_mem[0x1FFF8 / 2 + 4]);

    /* Borrowed (but owned) also clears. */
    setup(SMD_DISP_TYPE_MONO_PORTRAIT);
    memset(test_display_mem, 0x5A, sizeof(test_display_mem));
    rec()->owner_asid = 7;
    rec()->borrowed_asid = 9;

    smd_$reset_unit_display(TEST_UNIT, (boolean)0xFF);

    CHECK_EQ(0x0000, test_display_mem[0x1FFF0 / 2]);
}

int main(void)
{
    printf("SMD hidden-display-memory tests\n");

    RUN_TEST(alloc_without_a_display);
    RUN_TEST(alloc_from_an_empty_list);
    RUN_TEST(alloc_splits_a_block);
    RUN_TEST(alloc_exact_fit_removes_the_block);
    RUN_TEST(alloc_landscape_position);
    RUN_TEST(free_validates_the_position);
    RUN_TEST(free_inserts_in_order);
    RUN_TEST(free_merges_with_the_previous_block);
    RUN_TEST(free_merges_with_the_next_block);
    RUN_TEST(free_merges_both_neighbours);
    RUN_TEST(alloc_then_free_round_trips);
    RUN_TEST(reset_clears_the_clip_window_and_fonts);
    RUN_TEST(reset_seeds_the_free_list);
    RUN_TEST(reset_seed_matches_the_free_hdm_bounds);
    RUN_TEST(reset_display_memory_clear_is_conditional);

    printf("\n%d tests, %d failed\n", tests_passed + tests_failed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
