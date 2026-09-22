/*
 * kbd/test/test_set_type.c - Unit tests for kbd_$set_type (0x00E1CA8C),
 * KBD_$SET_KBD_TYPE (0x00E72524), KBD_$SET_KBD_MODE (0x00E72516) and
 * KBD_$RESET (0x00E1AB28)
 *
 * The real .c files are #included with kbd_data.c (MNK_$KTT_PTRS /
 * MNK_$KTT_MAX); KBD_$GET_DESC is mocked.
 */

#include "kbd/kbd_internal.h"

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
#define ASSERT_PTR_EQ(e, a) ASSERT_EQ((uintptr_t)(e), (uintptr_t)(a))

uint8_t SMD_$KTT[0x100];
uint8_t SMD_$GERMAN_KTT[0x100];
uint8_t SMD_$FRENCH_KTT[0x100];
uint8_t SMD_$SWEDISH_KTT[0x100];
uint8_t SMD_$UK_KTT[0x100];
uint8_t SMD_$SWISS_KTT[0x100];

static kbd_state_t desc;
static status_$t get_desc_status;
static uint16_t *get_desc_line;
void *KBD_$GET_DESC(uint16_t *line_ptr, status_$t *status_ret)
{
    get_desc_line = line_ptr;
    *status_ret = get_desc_status;
    return &desc;
}

#include "../kbd_data.c"
#include "../set_type.c"
#include "../set_kbd_type.c"
#include "../set_kbd_mode.c"
#include "../reset.c"

static void reset(void)
{
    memset(&desc, 0xAA, sizeof(desc));
    get_desc_status = status_$ok;
}

TEST(two_byte_type_selects_table)
{
    uint8_t s[] = { 'U', 'C' };         /* 'C' - 0x40 = 3 */
    reset();
    kbd_$set_type(&desc, s, 2);
    ASSERT_EQ('U', desc.kbd_type_str[0]);
    ASSERT_EQ('C', desc.kbd_type_str[1]);
    ASSERT_EQ(2, desc.kbd_type_len);
    ASSERT_PTR_EQ(MNK_$KTT_PTRS[3], desc.ktt_ptr);
}

TEST(length_is_clamped_to_two)
{
    uint8_t s[] = { 'A', 'B', 'C', 'D' };
    reset();
    kbd_$set_type(&desc, s, 4);
    ASSERT_EQ('A', desc.kbd_type_str[0]);
    ASSERT_EQ('B', desc.kbd_type_str[1]);
    ASSERT_EQ(0xAA, desc.kbd_type_str[2]);      /* untouched */
    ASSERT_EQ(2, desc.kbd_type_len);
}

TEST(one_byte_type_zeroes_second_but_reads_it_for_the_table)
{
    uint8_t s[] = { '2', 'G' };         /* 'G' - 0x40 = 7 = MNK_$KTT_MAX */
    reset();
    kbd_$set_type(&desc, s, 1);
    ASSERT_EQ('2', desc.kbd_type_str[0]);
    ASSERT_EQ(0, desc.kbd_type_str[1]);
    ASSERT_EQ(1, desc.kbd_type_len);
    ASSERT_PTR_EQ(MNK_$KTT_PTRS[7], desc.ktt_ptr);
}

TEST(zero_length_copies_nothing)
{
    uint8_t s[] = { 'X', 0x40 };
    reset();
    kbd_$set_type(&desc, s, 0);
    ASSERT_EQ(0xAA, desc.kbd_type_str[0]);
    ASSERT_EQ(0, desc.kbd_type_str[1]);
    ASSERT_EQ(0, desc.kbd_type_len);
    ASSERT_PTR_EQ(MNK_$KTT_PTRS[0], desc.ktt_ptr);
}

TEST(out_of_range_second_byte_falls_back_to_entry_zero)
{
    uint8_t lo[] = { 'U', 0x3F };       /* -1 */
    uint8_t hi[] = { 'U', 0x48 };       /* 8 > MNK_$KTT_MAX */
    uint8_t top[] = { 'U', 0xFF };      /* zero-extended: 0xBF, positive */
    reset();
    kbd_$set_type(&desc, lo, 2);
    ASSERT_PTR_EQ(MNK_$KTT_PTRS[0], desc.ktt_ptr);
    kbd_$set_type(&desc, hi, 2);
    ASSERT_PTR_EQ(MNK_$KTT_PTRS[0], desc.ktt_ptr);
    desc.ktt_ptr = NULL;
    kbd_$set_type(&desc, top, 2);
    ASSERT_PTR_EQ(MNK_$KTT_PTRS[0], desc.ktt_ptr);
}

TEST(set_kbd_type_wrapper)
{
    uint16_t line = 2, len = 2;
    uint8_t s[] = { 'F', 'B' };
    status_$t status = 0x55;

    reset();
    KBD_$SET_KBD_TYPE(&line, s, &len, &status);
    ASSERT_PTR_EQ(&line, get_desc_line);
    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ('F', desc.kbd_type_str[0]);
    ASSERT_PTR_EQ(MNK_$KTT_PTRS[2], desc.ktt_ptr);

    reset();
    get_desc_status = status_$invalid_line_number;
    KBD_$SET_KBD_TYPE(&line, s, &len, &status);
    ASSERT_EQ(status_$invalid_line_number, status);
    ASSERT_EQ(0xAA, desc.kbd_type_str[0]);
}

TEST(set_kbd_mode_and_reset)
{
    short line = 9;
    unsigned char mode = 3;
    status_$t status = 0x55;
    KBD_$SET_KBD_MODE(&line, &mode, &status);
    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(9, line);
    ASSERT_EQ(3, mode);
    KBD_$RESET();
}

int main(void)
{
    RUN_TEST(two_byte_type_selects_table);
    RUN_TEST(length_is_clamped_to_two);
    RUN_TEST(one_byte_type_zeroes_second_but_reads_it_for_the_table);
    RUN_TEST(zero_length_copies_nothing);
    RUN_TEST(out_of_range_second_byte_falls_back_to_entry_zero);
    RUN_TEST(set_kbd_type_wrapper);
    RUN_TEST(set_kbd_mode_and_reset);
    printf("%d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed ? 1 : 0;
}
