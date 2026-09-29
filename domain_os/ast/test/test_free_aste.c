/*
 * ast/test/test_free_aste.c - Unit tests for AST_$FREE_ASTE (0x00E00FBC)
 *                              and ast_$flush_installed_pages (0x00E03FBC)
 *
 * Both are small AST helpers with no control flow beyond a flag test; the
 * test #includes both .c files and drives them through mocks.  It pins:
 *
 *   - FREE_ASTE's counter selection (bit 12 AREA before bit 11 remote,
 *     else local), the free-list push, the bit-15 marker and the
 *     eventcount advance;
 *   - flush_installed_pages' argument forwarding, the BYTE subtraction on
 *     the ASTE page count, and the count being zeroed.
 */

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define uid_t ast_uid_t

static int tests_passed = 0;
static int tests_failed = 0;
static int current_failed = 0;

static void reset_state(void);

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do {                                                   \
    printf("  Running %-48s", #name);                                         \
    current_failed = 0;                                                       \
    reset_state();                                                            \
    test_##name();                                                            \
    if (current_failed == 0) { tests_passed++; printf("PASSED\n"); }          \
} while (0)

#define ASSERT_EQ(expected, actual) do {                                      \
    if ((unsigned long long)(expected) != (unsigned long long)(actual)) {      \
        printf("FAILED\n    Expected: 0x%llx, Got: 0x%llx at line %d\n",       \
               (unsigned long long)(expected),                                \
               (unsigned long long)(actual), __LINE__);                       \
        tests_failed++; current_failed = 1;                                   \
        return;                                                               \
    }                                                                         \
} while (0)

#include "ast/free_aste.c"
#include "ast/flush_installed_pages.c"

/* AST_ block cells */
/* The AST_ module blocks (ast/ast.h). */
MODULE_DATA_DEFINE(ast_$data_t, AST_$DATA, 0x00E1DC80);
MODULE_DATA_DEFINE(ast_$aot_t, AST_$AOT, 0x00EC5400);
uint16_t  PROC1_$CURRENT;

static int advance_calls;
static ec_$eventcount_t *advance_ec;
void EC_$ADVANCE(ec_$eventcount_t *ec) { advance_calls++; advance_ec = ec; }

static int       remove_list_calls;
static uint32_t *remove_list_array;
static uint16_t  remove_list_count;
void MMU_$REMOVE_LIST(uint32_t *ppn_array, uint16_t count)
{
    remove_list_calls++;
    remove_list_array = ppn_array;
    remove_list_count = count;
}

static int       free_pages_calls;
static uint32_t *free_pages_array;
static uint16_t  free_pages_count;
void MMAP_$FREE_PAGES(uint16_t pid, uint32_t *vpn_array, uint16_t count)
{
    (void)pid;
    free_pages_calls++;
    free_pages_array = vpn_array;
    free_pages_count = count;
}

static void reset_state(void)
{
    AST_$FREE_ASTE_HEAD = NULL;
    AST_$FREE_ASTES = 5;
    AST_$ASTE_AREA_CNT = 10;
    AST_$ASTE_R_CNT = 20;
    AST_$ASTE_L_CNT = 30;
    advance_calls = 0; advance_ec = NULL;
    remove_list_calls = 0; free_pages_calls = 0;
    PROC1_$CURRENT = 3;
}

TEST(free_area_aste)
{
    aste_t a, old_head;
    memset(&a, 0, sizeof(a)); memset(&old_head, 0, sizeof(old_head));
    a.flags = ASTE_FLAG_AREA | ASTE_FLAG_REMOTE;   /* AREA wins */
    a.aote = (aote_t *)&old_head;
    AST_$FREE_ASTE_HEAD = &old_head;

    AST_$FREE_ASTE(&a);

    ASSERT_EQ(9, AST_$ASTE_AREA_CNT);
    ASSERT_EQ(20, AST_$ASTE_R_CNT);
    ASSERT_EQ(30, AST_$ASTE_L_CNT);
    ASSERT_EQ((uintptr_t)NULL, (uintptr_t)a.aote);
    ASSERT_EQ((uintptr_t)&old_head, (uintptr_t)a.next);
    ASSERT_EQ((uintptr_t)&a, (uintptr_t)AST_$FREE_ASTE_HEAD);
    ASSERT_EQ(ASTE_FLAG_AREA | ASTE_FLAG_REMOTE | ASTE_FLAG_IN_TRANS, a.flags);
    ASSERT_EQ(6, AST_$FREE_ASTES);
    ASSERT_EQ(1, advance_calls);
    ASSERT_EQ((uintptr_t)&AST_$AST_IN_TRANS_EC, (uintptr_t)advance_ec);
}

TEST(free_remote_aste)
{
    aste_t a;
    memset(&a, 0, sizeof(a));
    a.flags = ASTE_FLAG_REMOTE;

    AST_$FREE_ASTE(&a);

    ASSERT_EQ(10, AST_$ASTE_AREA_CNT);
    ASSERT_EQ(19, AST_$ASTE_R_CNT);
    ASSERT_EQ(30, AST_$ASTE_L_CNT);
}

TEST(free_local_aste)
{
    aste_t a;
    memset(&a, 0, sizeof(a));
    a.flags = ASTE_FLAG_DIRTY;

    AST_$FREE_ASTE(&a);

    ASSERT_EQ(10, AST_$ASTE_AREA_CNT);
    ASSERT_EQ(20, AST_$ASTE_R_CNT);
    ASSERT_EQ(29, AST_$ASTE_L_CNT);
    ASSERT_EQ(ASTE_FLAG_DIRTY | ASTE_FLAG_IN_TRANS, a.flags);
}

TEST(flush_forwards_and_subtracts_byte)
{
    aste_t a;
    uint32_t ppns[4] = { 0x300, 0x301, 0x302, 0x303 };
    uint16_t count = 3;
    memset(&a, 0, sizeof(a));
    a.page_count = 2;                    /* 2 - 3 wraps in a byte */

    ast_$flush_installed_pages(&a, ppns, &count);

    ASSERT_EQ(1, remove_list_calls);
    ASSERT_EQ((uintptr_t)ppns, (uintptr_t)remove_list_array);
    ASSERT_EQ(3, remove_list_count);
    ASSERT_EQ(1, free_pages_calls);
    ASSERT_EQ((uintptr_t)ppns, (uintptr_t)free_pages_array);
    ASSERT_EQ(3, free_pages_count);
    ASSERT_EQ(0xFF, a.page_count);
    ASSERT_EQ(0, count);
}

int main(void)
{
    printf("test_free_aste (AST_$FREE_ASTE 0x00E00FBC, ast_$flush_installed_pages 0x00E03FBC)\n");

    RUN_TEST(free_area_aste);
    RUN_TEST(free_remote_aste);
    RUN_TEST(free_local_aste);
    RUN_TEST(flush_forwards_and_subtracts_byte);

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
