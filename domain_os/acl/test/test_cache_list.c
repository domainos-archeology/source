/*
 * acl/test/test_cache_list.c - unit tests for acl_$cache_list_insert
 * (0x00E44C3C) and acl_$cache_list_remove (0x00E44C92).
 *
 * The real acl/cache_list.c is #included; both routines are pure list
 * surgery on a caller-supplied head word and link array, so no mocks are
 * needed.  Each test pins one branch of the disassembly.
 */

#include <stdio.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;
static int current_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do {                     \
    printf("  %-46s ", #name);                  \
    current_failed = 0;                         \
    test_##name();                              \
    if (current_failed) { tests_failed++; }     \
    else { tests_passed++; printf("PASSED\n"); }\
} while (0)

#define ASSERT_EQ(expected, actual) do {                                 \
    long long _e = (long long)(expected);                                \
    long long _a = (long long)(actual);                                  \
    if (_e != _a) {                                                      \
        printf("FAILED\n    Expected %lld, got %lld at line %d\n",       \
               _e, _a, __LINE__);                                        \
        current_failed = 1;                                              \
        return;                                                          \
    }                                                                    \
} while (0)

#include "acl/acl_internal.h"
#include "../cache_list.c"

static int16_t           head;
static acl_$cache_link_t links[8];

static void reset(void)
{
    head = ACL_CACHE_NO_SLOT;
    memset(links, 0x7f, sizeof(links));     /* poison: 0x7f7f */
}

/* 0x00E44C7A..0x00E44C88: insert into an empty list makes a 1-ring. */
TEST(insert_into_empty_list)
{
    reset();
    acl_$cache_list_insert(&head, links, 3);
    ASSERT_EQ(3, head);
    ASSERT_EQ(3, links[3].next);
    ASSERT_EQ(3, links[3].prev);
}

/* 0x00E44C54..0x00E44C78: a second insert splices before the old head
 * (i.e. at the ring tail) and then takes over as head. */
TEST(insert_splices_at_tail_and_becomes_head)
{
    reset();
    acl_$cache_list_insert(&head, links, 3);
    acl_$cache_list_insert(&head, links, 5);
    ASSERT_EQ(5, head);
    ASSERT_EQ(3, links[5].next);
    ASSERT_EQ(3, links[5].prev);
    ASSERT_EQ(5, links[3].next);
    ASSERT_EQ(5, links[3].prev);

    acl_$cache_list_insert(&head, links, 1);
    /* ring is 1 -> 5 -> 3 -> 1 */
    ASSERT_EQ(1, head);
    ASSERT_EQ(5, links[1].next);
    ASSERT_EQ(3, links[1].prev);
    ASSERT_EQ(1, links[3].next);
    ASSERT_EQ(5, links[3].prev);
    ASSERT_EQ(3, links[5].next);
    ASSERT_EQ(1, links[5].prev);
}

/* 0x00E44CA6: a -1 head is left completely alone, links untouched. */
TEST(remove_from_empty_list_is_noop)
{
    reset();
    acl_$cache_list_remove(&head, links, 2);
    ASSERT_EQ(ACL_CACHE_NO_SLOT, head);
    ASSERT_EQ(0x7f7f, links[2].next);
    ASSERT_EQ(0x7f7f, links[2].prev);
}

/* 0x00E44CCE/0x00E44CD2/0x00E44CD6: removing the only member empties the list. */
TEST(remove_sole_member_empties_list)
{
    reset();
    acl_$cache_list_insert(&head, links, 4);
    acl_$cache_list_remove(&head, links, 4);
    ASSERT_EQ(ACL_CACHE_NO_SLOT, head);
    /* 0x00E44CC0/0x00E44CCA rewrite the node's own links (self ring) but
     * nothing clears them. */
    ASSERT_EQ(4, links[4].next);
    ASSERT_EQ(4, links[4].prev);
}

/* 0x00E44CDC: removing the head of a longer ring advances head to next. */
TEST(remove_head_advances_to_next)
{
    reset();
    acl_$cache_list_insert(&head, links, 3);
    acl_$cache_list_insert(&head, links, 5);
    acl_$cache_list_insert(&head, links, 1);    /* 1 -> 5 -> 3 */
    acl_$cache_list_remove(&head, links, 1);
    ASSERT_EQ(5, head);
    ASSERT_EQ(3, links[5].next);
    ASSERT_EQ(3, links[5].prev);
    ASSERT_EQ(5, links[3].next);
    ASSERT_EQ(5, links[3].prev);
    /* the removed node keeps its stale links (0x00E44CB2/0x00E44CB6 only read) */
    ASSERT_EQ(5, links[1].next);
    ASSERT_EQ(3, links[1].prev);
}

/* 0x00E44CCE bne: removing a non-head member leaves head as is. */
TEST(remove_non_head_keeps_head)
{
    reset();
    acl_$cache_list_insert(&head, links, 3);
    acl_$cache_list_insert(&head, links, 5);
    acl_$cache_list_insert(&head, links, 1);    /* 1 -> 5 -> 3 */
    acl_$cache_list_remove(&head, links, 5);
    ASSERT_EQ(1, head);
    ASSERT_EQ(3, links[1].next);
    ASSERT_EQ(3, links[1].prev);
    ASSERT_EQ(1, links[3].next);
    ASSERT_EQ(1, links[3].prev);
}

int main(void)
{
    printf("acl_$cache_list_insert / acl_$cache_list_remove tests\n");
    RUN_TEST(insert_into_empty_list);
    RUN_TEST(insert_splices_at_tail_and_becomes_head);
    RUN_TEST(remove_from_empty_list_is_noop);
    RUN_TEST(remove_sole_member_empties_list);
    RUN_TEST(remove_head_advances_to_next);
    RUN_TEST(remove_non_head_keeps_head);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
