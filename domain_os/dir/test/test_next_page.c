/*
 * dir/test/test_next_page.c - dir_$next_page (0x00E4D7B0)
 *
 * Pins: the deepest interior level with an entry left steps to it and the
 * levels below restart at their first entry; the root page index table
 * starts after its root area; an exhausted tree (or depth < 2) gives 0xFFFF.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    int _before = tests_failed; \
    printf("  Running %s... ", #name); \
    fflush(stdout); \
    test_##name(); \
    if (tests_failed == _before) { tests_passed++; printf("PASSED\n"); } \
} while (0)

#define ASSERT_EQ(expected, actual) do { \
    unsigned long _e = (unsigned long)(expected); \
    unsigned long _a = (unsigned long)(actual); \
    if (_e != _a) { \
        printf("FAILED\n    Expected: 0x%lx, Got: 0x%lx at line %d\n", \
               _e, _a, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

#define TEST_SUMMARY() do { \
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed); \
    return tests_failed ? 1 : 0; \
} while (0)

#include "dir/dir_internal.h"

static uint8_t pages[16][0x400] __attribute__((aligned(4)));
static int nmap;

void *dir_$map_page(void *handle, int16_t page_idx)
{
    (void)handle;
    nmap++;
    return pages[page_idx];
}

#include "../next_page.c"

static void w(int pg, int off, uint16_t v) { *(uint16_t *)(pages[pg] + off) = v; }

/* page pg with `n' index entries starting at `base', entry k at
 * 0x100 + 0x10*k pointing at child[k] */
static void mkpage(int pg, int base, int n, const uint16_t *child)
{
    int k;
    w(pg, 0x0A, (uint16_t)pg);
    w(pg, 0x0E, (uint16_t)(base + 2 * n));
    for (k = 0; k < n; k++) {
        w(pg, base + 2 * k, (uint16_t)(0x100 + 0x10 * k));
        w(pg, 0x100 + 0x10 * k + 2, child[k]);
    }
}

static void setup(void)
{
    static const uint16_t c0[] = { 5, 6 };
    static const uint16_t c5[] = { 10, 11 };
    static const uint16_t c6[] = { 12 };
    memset(pages, 0, sizeof(pages));
    w(0, 0x14, 4);                              /* root area: base 0x16 */
    mkpage(0, 0x16, 2, c0);
    mkpage(5, 0x12, 2, c5);
    mkpage(6, 0x12, 1, c6);
    nmap = 0;
}

TEST(next_in_same_parent)
{
    dir_$page_path_t path[2] = { { 0, 1 }, { 5, 1 } };
    uint16_t pg = 0;
    setup();
    dir_$next_page(NULL, 3, path, &pg);
    ASSERT_EQ(11, pg);
    ASSERT_EQ(5, path[1].page_no);
    ASSERT_EQ(2, path[1].entry_idx);
    ASSERT_EQ(1, path[0].entry_idx);
}

TEST(climb_and_descend)
{
    dir_$page_path_t path[2] = { { 0, 1 }, { 5, 2 } };
    uint16_t pg = 0;
    setup();
    dir_$next_page(NULL, 3, path, &pg);
    ASSERT_EQ(12, pg);
    ASSERT_EQ(2, path[0].entry_idx);
    ASSERT_EQ(6, path[1].page_no);
    ASSERT_EQ(1, path[1].entry_idx);
}

TEST(exhausted)
{
    dir_$page_path_t path[2] = { { 0, 2 }, { 6, 1 } };
    uint16_t pg = 0;
    setup();
    dir_$next_page(NULL, 3, path, &pg);
    ASSERT_EQ(0xFFFF, pg);
    ASSERT_EQ(2, nmap);
}

TEST(shallow_tree)
{
    dir_$page_path_t path[1] = { { 0, 1 } };
    uint16_t pg = 0;
    setup();
    dir_$next_page(NULL, 1, path, &pg);
    ASSERT_EQ(0xFFFF, pg);
    ASSERT_EQ(0, nmap);
}

int main(void)
{
    printf("dir_$next_page tests\n");
    RUN_TEST(next_in_same_parent);
    RUN_TEST(climb_and_descend);
    RUN_TEST(exhausted);
    RUN_TEST(shallow_tree);
    TEST_SUMMARY();
}
