/*
 * smd/test/test_lock_cells.c - Unit tests for the by-reference constant
 * cells three SMD routines hand to their callees.
 *
 * Three real .c files are #included below and the real functions are called.
 *
 * Facts under test:
 *
 *   SMD_$CLEAR_WINDOW (body 0x00E706C0, gate 0x00E8495C) - bead source-hfa2
 *     "pea (0x24,A5)" at 0x00E706F2 with A5 = 0x00E84934 (the gate's
 *     "lea (-0x2a,PC),A0") pushes 0x00E84958, whose image bytes are
 *     "00 01 00 00"; SMD_$ACQ_DISPLAY reads it as a word ("cmpi.w #0x1,(A2)"
 *     at 0x00E6EB92), so the value it sees is 1.
 *
 *   SMD_$CLEAR_KBD_CURSOR (0x00E6E828) - bead source-4sio
 *     "pea (-0x842,PC)" at 0x00E6E838 -> 0x00E6DFF8, the shared word 1 that
 *     smd_internal.h names SMD_SYNC_LOCK_DATA.
 *
 *   SMD_$UNBLANK (0x00E6EFB4) - bead source-m87d
 *     "pea (-0xb8a,PC)" at 0x00E6EFE0 -> 0x00E6E458, the shared Domain
 *     boolean 0xFF that smd_internal.h names SMD_TRUE_DATA.
 *
 * In every case the file used to declare a private object whose VALUE was the
 * cell's ADDRESS and pass that object's address, so the callee read the high
 * half of an address instead of the constant.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "smd/smd_internal.h"
#include "time/time.h"

/* ------------------------------------------------------------------ */
/* Test harness                                                        */
/* ------------------------------------------------------------------ */

static int tests_passed = 0;
static int tests_failed = 0;
static int current_failed = 0;

#define TEST(name) static void test_##name(void)
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

/* ------------------------------------------------------------------ */
/* Mocked globals                                                      */
/* ------------------------------------------------------------------ */

smd_globals_t SMD_GLOBALS;
uint16_t PROC1_$AS_ID;
ec_$eventcount_t TIME_$CLOCKH_EC = { .value = (int32_t)(0) };  /* TIME_$CLOCKH = its value */

/* The two shared code-region cells, at their image values (smd/smd_data.c). */
int16_t SMD_SYNC_LOCK_DATA = 1;         /* 0x00E6DFF8: "00 01" */
const boolean SMD_TRUE_DATA = true;     /* 0x00E6E458: "ff"    */

/* ------------------------------------------------------------------ */
/* Mocked callees                                                      */
/* ------------------------------------------------------------------ */

static status_$t mock_util_status;
static smd_hw_blt_regs_t mock_blt_regs;
static int acq_calls, rel_calls;
static int16_t acq_seen_value;

void SMD_$UTIL_INIT(smd_util_ctx_t *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->ctrl_regs = &mock_blt_regs;
    ctx->status = mock_util_status;
}

uint16_t SMD_$ACQ_DISPLAY(int16_t *lock_data)
{
    acq_calls++;
    acq_seen_value = *lock_data;
    return 0;
}

void SMD_$REL_DISPLAY(void)
{
    rel_calls++;
}

static int add_trk_calls;
static smd_track_rect_t *add_trk_seen_rect;
static uint16_t add_trk_seen_id;

void SMD_$ADD_TRK_RECT(smd_track_rect_t *rect, uint16_t *id,
                       status_$t *status_ret)
{
    add_trk_calls++;
    add_trk_seen_rect = rect;
    add_trk_seen_id = *id;
    *status_ret = 0;
}

static int video_calls;
static uint8_t video_seen_flags;

void SMD_$VIDEO_CTL(uint8_t *flags, status_$t *status_ret)
{
    video_calls++;
    video_seen_flags = *flags;
    *status_ret = 0;
}

/* ------------------------------------------------------------------ */
/* Functions under test                                                */
/* ------------------------------------------------------------------ */

#include "../clear_window.c"
#include "../clear_kbd_cursor.c"
#include "../unblank.c"

/* ------------------------------------------------------------------ */
/* Tests                                                               */
/* ------------------------------------------------------------------ */

TEST(clear_window_cell_is_the_word_one)
{
    /* The cell itself, not its address: image bytes "00 01 00 00" at
     * 0x00E84958, read as a word by SMD_$ACQ_DISPLAY. */
    CHECK_EQ(1, smd_$clear_window_lock_data);
    CHECK_EQ(2, (long)sizeof(smd_$clear_window_lock_data));
}

TEST(clear_window_error_path_takes_no_lock)
{
    smd_rect_t rect = { 0, 0, 0, 0 };
    status_$t st = 0;

    acq_calls = rel_calls = 0;
    mock_util_status = 0x00130004;

    SMD_$CLEAR_WINDOW(&rect, &st);

    /* 0x00E706DA "move.l (-0xc,A6),(A0)" / 0x00E706DE "bne.w" */
    CHECK_EQ(0x00130004, st);
    CHECK_EQ(0, acq_calls);
    CHECK_EQ(0, rel_calls);
}

TEST(clear_kbd_cursor_passes_the_sync_lock_cell)
{
    status_$t st = 0x7F7F7F7F;

    add_trk_calls = 0;
    add_trk_seen_rect = NULL;
    add_trk_seen_id = 0xFFFF;

    SMD_$CLEAR_KBD_CURSOR(&st);

    CHECK_EQ(1, add_trk_calls);
    /* the value 1, not the low half of 0x00E6DFF8 */
    CHECK_EQ(1, add_trk_seen_id);
    /* "pea (0xc0,A5)" -> SMD_GLOBALS + 0xC0 */
    CHECK_EQ((uintptr_t)&SMD_GLOBALS.kbd_cursor_track_rect,
             (uintptr_t)add_trk_seen_rect);
    CHECK_EQ(0, st);
}

TEST(unblank_stamps_the_clock_only_when_not_pending)
{
    video_calls = 0;
    TIME_$CLOCKH = 0x12345678;
    SMD_GLOBALS.blank_time = 0;
    SMD_GLOBALS.blank_pending = false;
    SMD_GLOBALS.blank_enabled = true;   /* the neighbouring byte, +0xDC */

    SMD_$UNBLANK();

    /* 0x00E6EFC0 always runs; 0x00E6EFC8 "tst.b (0xdd,A5)" is blank_pending */
    CHECK_EQ(0x12345678, SMD_GLOBALS.blank_time);
    CHECK_EQ(0, video_calls);
}

TEST(unblank_passes_the_shared_true_byte)
{
    video_calls = 0;
    video_seen_flags = 0;
    TIME_$CLOCKH = 0x0BADC0DE;
    PROC1_$AS_ID = 5;
    SMD_GLOBALS.default_unit = 1;
    SMD_GLOBALS.asid_to_unit[5] = 0;
    SMD_GLOBALS.blank_pending = true;   /* 0xFF, i.e. < 0 */

    SMD_$UNBLANK();

    CHECK_EQ(0x0BADC0DE, SMD_GLOBALS.blank_time);
    CHECK_EQ(1, SMD_GLOBALS.asid_to_unit[5]);
    CHECK_EQ(1, video_calls);
    /* 0xFF, not 0x80 and not the low half of 0x00E6E458 */
    CHECK_EQ(0xFF, video_seen_flags);
}

int main(void)
{
    printf("test_lock_cells:\n");

    RUN_TEST(clear_window_cell_is_the_word_one);
    RUN_TEST(clear_window_error_path_takes_no_lock);
    RUN_TEST(clear_kbd_cursor_passes_the_sync_lock_cell);
    RUN_TEST(unblank_stamps_the_clock_only_when_not_pending);
    RUN_TEST(unblank_passes_the_shared_true_byte);

    printf("\n  Results: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
