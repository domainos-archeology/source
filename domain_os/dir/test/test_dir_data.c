/*
 * dir/test/test_dir_data.c - the DIR module block's tables, 0x00E7DBF8+0x212C
 *
 * Two things the image pins and nothing else in dir/ asserts:
 *   - DIR_$OP_TAB is one *biased* table.  Every reader indexes it by
 *     (opcode >> 1) from a virtual base of 0x00E7FB9A, which is 21 records
 *     below the array; only records 21..46 (opcodes 0x2A..0x5C, the range
 *     DIR_$DO_OP's jump table admits) are physically present.
 *   - the last 0x12 bytes of the segment, 0x00E7FD12..0x00E7FD24.
 */

#include "dir/dir_internal.h"

#include "../dir_data.c"

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
        printf("%-48s %s\n", #name, current_failed ? "FAIL" : "ok");          \
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

#define DIR_OP_TAB_ADDR      0x00E7FC42
#define DIR_OP_FAMILY_BASE   0x00E7FB9A

/* 0x00E7FC42 - 0x00E7FB9A = 0xA8 = 21 records of 8 bytes. */
TEST(the_bias_is_the_distance_to_the_virtual_base)
{
    ASSERT_EQ(8, sizeof(dir_$op_tab_entry_t));
    ASSERT_EQ(DIR_$OP_TAB_BASE_INDEX * (int)sizeof(dir_$op_tab_entry_t),
              DIR_OP_TAB_ADDR - DIR_OP_FAMILY_BASE);
}

/* Opcodes 0x2A..0x5C -> records 21..46, and the array covers exactly those. */
TEST(the_table_covers_every_opcode_do_op_admits)
{
    ASSERT_EQ(DIR_$OP_TAB_BASE_INDEX, 0x2A >> 1);
    ASSERT_EQ(DIR_$OP_TAB_ENTRIES - 1, (0x5C >> 1) - DIR_$OP_TAB_BASE_INDEX);
    ASSERT_EQ(0xD0, sizeof(DIR_$OP_TAB));
    ASSERT_EQ(0x00E7FD12 - DIR_OP_TAB_ADDR, (int)sizeof(DIR_$OP_TAB));
}

/*
 * DIR_$ADD_MOUNT reads 0x00E7FD02 / 0x00E7FD06 and DIR_$DROP_MOUNT
 * 0x00E7FD0A / 0x00E7FD0E; both are the last two records.  DIR_$GET_ENTRYU
 * reaches record 34 through A5 as (0x20aa,A5) / (0x20ae,A5) = 0x00E7FCAA /
 * 0x00E7FCAE, whose image words are 0x0000 and 0x0002.
 */
TEST(the_named_records_line_up_with_their_addresses)
{
    const char *base = (const char *)DIR_$OP_TAB;

    ASSERT_EQ(0x00E7FD02 - DIR_OP_TAB_ADDR,
              (const char *)&DIR_$OP_REC(0x5A >> 1).version - base);
    ASSERT_EQ(0x00E7FD06 - DIR_OP_TAB_ADDR,
              (const char *)&DIR_$OP_REC(0x5A >> 1).base_size - base);
    ASSERT_EQ(0x00E7FD0A - DIR_OP_TAB_ADDR,
              (const char *)&DIR_$OP_REC(0x5C >> 1).version - base);
    ASSERT_EQ(0x00E7FD0E - DIR_OP_TAB_ADDR,
              (const char *)&DIR_$OP_REC(0x5C >> 1).base_size - base);

    ASSERT_EQ(0x0000, DIR_$OP_REC(0x44 >> 1).version);
    ASSERT_EQ(0x0002, DIR_$OP_REC(0x44 >> 1).base_size);
    ASSERT_EQ(0x000c, DAT_00e7fd06);
    ASSERT_EQ(0x000c, DAT_00e7fd0e);
}

/*
 * DIR_$DO_OP's (0x1f9c,A0) and (0x1fa0,A0) with A5 = 0x00E7DC00 are record
 * + 0x02 and record + 0x06 of the same records, so the two DIR_$OP_ macros
 * must land two and six bytes into the record.
 */
TEST(do_ops_a5_displacements_are_the_reply_fields)
{
    const char *rec = (const char *)&DIR_$OP_REC(0x30 >> 1);
    ASSERT_EQ(2, (const char *)&DIR_$OP_VERSION(0x30 >> 1) - rec);
    ASSERT_EQ(6, (const char *)&DIR_$OP_REPLY_SIZE(0x30 >> 1) - rec);
    ASSERT_EQ(0, (const char *)&DIR_$OP_REQ_VERSION(0x30 >> 1) - rec);
}

/* 0x00E7FD12..0x00E7FD24, the segment's last 0x12 bytes. */
TEST(the_segment_tail_holds_its_image_bytes)
{
    ASSERT_EQ(0x12, (int)(sizeof(DAT_00e7fd12) + sizeof(DAT_00e7fd14)
                          + sizeof(DAT_00e7fd18) + sizeof(DAT_00e7fd1c)
                          + sizeof(DAT_00e7fd20)));
    ASSERT_EQ(0x0000, DAT_00e7fd12);
    ASSERT_EQ(0x00020001u, DAT_00e7fd14);
    ASSERT_EQ(0x00010001u, DAT_00e7fd18);
    ASSERT_EQ(0x00000001u, DAT_00e7fd1c);
    ASSERT_EQ(0, memcmp(DAT_00e7fd20, ".bak", 4));
}

int main(void)
{
    printf("=== DIR module block tables ===\n");
    RUN_TEST(the_bias_is_the_distance_to_the_virtual_base);
    RUN_TEST(the_table_covers_every_opcode_do_op_admits);
    RUN_TEST(the_named_records_line_up_with_their_addresses);
    RUN_TEST(do_ops_a5_displacements_are_the_reply_fields);
    RUN_TEST(the_segment_tail_holds_its_image_bytes);
    printf("%d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed != 0;
}
