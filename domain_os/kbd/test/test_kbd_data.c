/*
 * kbd/test/test_kbd_data.c - the KBD module block, 0x00E2DDE4..0x00E2DE3C
 *
 * Pins the split of the two word tables that follow KBD_$MODE_TABLE.  The
 * eight words at 0x00E2DDEC are what KBD_$RCV (0x00E1CE30) and kbd_$fetch_key
 * (0x00E1CBDA) index; the 32 words at 0x00E2DDFC are a separate object that
 * nothing in the image reads.
 */

#include "kbd/kbd_internal.h"

/* The KTT tables MNK_$KTT_PTRS points at live in smd/smd_data.c; only their
 * addresses matter here, so stub the storage. */
uint8_t SMD_$KTT[0x100];
uint8_t SMD_$GERMAN_KTT[0x100];
uint8_t SMD_$FRENCH_KTT[0x100];
uint8_t SMD_$SWEDISH_KTT[0x100];
uint8_t SMD_$UK_KTT[0x100];
uint8_t SMD_$SWISS_KTT[0x100];

#include "../kbd_data.c"

#include <stdio.h>
#include <string.h>

static int tests_run = 0;
static int tests_failed = 0;
static int current_failed;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name)                                                        \
    do {                                                                      \
        current_failed = 0;                                                   \
        tests_run++;                                                          \
        test_##name();                                                        \
        printf("%-44s %s\n", #name, current_failed ? "FAIL" : "ok");          \
    } while (0)
#define ASSERT_EQ(expected, actual)                                           \
    do {                                                                      \
        long long e_ = (long long)(expected), a_ = (long long)(actual);       \
        if (e_ != a_) {                                                       \
            printf("  %s:%d: expected 0x%llx, got 0x%llx\n",                  \
                   __FILE__, __LINE__, (unsigned long long)e_,                \
                   (unsigned long long)a_);                                   \
            if (!current_failed) { current_failed = 1; tests_failed++; }      \
        }                                                                     \
    } while (0)

/* 0x00E2DDE4, image bytes 00 01 02 03 12 10 11 0F. */
TEST(mode_table_is_eight_bytes)
{
    static const uint8_t image[8] = { 0x00, 0x01, 0x02, 0x03,
                                      0x12, 0x10, 0x11, 0x0f };
    ASSERT_EQ(8, sizeof(KBD_$MODE_TABLE));
    ASSERT_EQ(0, memcmp(KBD_$MODE_TABLE, image, sizeof(image)));
}

/*
 * 0x00E2DDEC..0x00E2DDFC.  Both readers double a word index whose range is
 * 0..MNK_$KTT_MAX, so the table has exactly MNK_$KTT_MAX + 1 entries.
 */
TEST(escape_state_table_covers_every_keyboard_type)
{
    static const uint16_t image[8] = { 0x0000, 0x0008, 0x0006, 0x0007,
                                       0x000e, 0x000e, 0x000e, 0x000e };
    ASSERT_EQ(0x10, sizeof(DAT_00e2ddec));
    ASSERT_EQ(MNK_$KTT_MAX + 1,
              (int)(sizeof(DAT_00e2ddec) / sizeof(DAT_00e2ddec[0])));
    ASSERT_EQ(0, memcmp(DAT_00e2ddec, image, sizeof(image)));
}

/* 0x00E2DDFC..0x00E2DE3C, the bytes up to TERM_$TPAD_BUFFER. */
TEST(trailing_words_fill_the_segment_to_tpad_buffer)
{
    ASSERT_EQ(0x40, sizeof(DAT_00e2ddfc));
    ASSERT_EQ(0x00E2DE3C - 0x00E2DDE4,
              (int)(sizeof(KBD_$MODE_TABLE) + sizeof(DAT_00e2ddec)
                    + sizeof(DAT_00e2ddfc)));
    /* First and last words of the image run. */
    ASSERT_EQ(0x0000, DAT_00e2ddfc[0]);
    ASSERT_EQ(0x0005, DAT_00e2ddfc[1]);
    ASSERT_EQ(0x0023, DAT_00e2ddfc[29]);
    ASSERT_EQ(0x0000, DAT_00e2ddfc[31]);
}

/* MNK_$KTT_PTRS is 8 longwords and MNK_$KTT_MAX its last index. */
TEST(ktt_pointer_table_matches_ktt_max)
{
    ASSERT_EQ(7, MNK_$KTT_MAX);
    ASSERT_EQ(MNK_$KTT_MAX + 1,
              (int)(sizeof(MNK_$KTT_PTRS) / sizeof(MNK_$KTT_PTRS[0])));
}

int main(void)
{
    printf("=== kbd module block (0x00E2DDE4) ===\n");
    RUN_TEST(mode_table_is_eight_bytes);
    RUN_TEST(escape_state_table_covers_every_keyboard_type);
    RUN_TEST(trailing_words_fill_the_segment_to_tpad_buffer);
    RUN_TEST(ktt_pointer_table_matches_ktt_max);
    printf("%d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed != 0;
}
