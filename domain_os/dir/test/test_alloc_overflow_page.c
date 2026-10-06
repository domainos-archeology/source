/*
 * dir/test/test_alloc_overflow_page.c - dir_$alloc_overflow_page
 * (0x00E4E960)
 *
 * Pins: the page after the last one in use is chosen (ctx->overflow_page);
 * when it is not below the object length the object grows to page+2 pages
 * through AST_$TRUNCATE; the page becomes a link page (first word 0, kind
 * bits 2) holding link_len bytes of link_data at +1; AST_$PURIFY(0x12, the
 * page) follows, and a failed purify invalidates the page and keeps the
 * purify status.
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

status_$t Naming_bad_request_header_ver_err = 0x000E0025;
static uint8_t arena[0x100] __attribute__((aligned(8)));
#define HVA 0x100
#define H ((dir_$handle_t *)arena)
static uint8_t pages[8][0x400] __attribute__((aligned(4)));
static int ncrash, ntrunc, npurify, ninval;
static uint32_t trunc_size_seen, purify_page_seen, inval_page_seen;
static uint16_t purify_flags_seen;
static status_$t trunc_status, purify_status;

void *dir_$map_page(void *handle, int16_t page_idx)
{
    (void)handle;
    return pages[page_idx];
}
void CRASH_SYSTEM(const status_$t *status) { (void)status; ncrash++; }
void AST_$TRUNCATE(uid_t *uid, uint32_t new_size, uint16_t flags,
                   boolean *result, status_$t *status)
{
    (void)uid; (void)flags; (void)result;
    ntrunc++;
    trunc_size_seen = new_size;
    *status = trunc_status;
}
uint16_t AST_$PURIFY(uid_t *uid, uint16_t flags, int16_t segment,
                     uint32_t *page_list, uint16_t list_count,
                     status_$t *status)
{
    (void)uid; (void)segment; (void)list_count;
    npurify++;
    purify_flags_seen = flags;
    purify_page_seen = *page_list;
    *status = purify_status;
    return 0;
}
void AST_$INVALIDATE(uid_t *uid, uint32_t start_page, uint32_t count,
                     boolean flags, status_$t *status)
{
    (void)uid; (void)count; (void)flags;
    ninval++;
    inval_page_seen = start_page;
    *status = 0;
}

#include "../alloc_overflow_page.c"

static dir_insert_ctx_t ctx;
static char text[] = "abcdefghij";

static void setup(uint32_t length, int used)
{
    int i;
    memset(arena, 0, sizeof(arena));
    memset(pages, 0, sizeof(pages));
    memset(&ctx, 0, sizeof(ctx));
    ARCH_HOST_VA_BASE = (uintptr_t)arena - HVA;
    H->length = length;
    for (i = 0; i < used; i++) *(uint16_t *)pages[i] = 0x0101;
    ctx.handle = HVA;
    ctx.link_data = text;
    ctx.link_len = 10;
    ncrash = ntrunc = npurify = ninval = 0;
    trunc_status = purify_status = 0;
}

TEST(reuses_room)
{
    status_$t st = 9;
    setup(0x1000, 2);
    dir_$alloc_overflow_page(&ctx, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(2, ctx.overflow_page);
    ASSERT_EQ(0, ntrunc);
    ASSERT_EQ(0x80, pages[2][0]);
    ASSERT_EQ(0, memcmp(pages[2] + 1, text, 10));
    ASSERT_EQ(1, npurify);
    ASSERT_EQ(0x12, purify_flags_seen);
    ASSERT_EQ(2, purify_page_seen);
    ASSERT_EQ(0, ninval);
}

TEST(grows_object)
{
    status_$t st = 9;
    setup(0x800, 2);
    dir_$alloc_overflow_page(&ctx, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(2, ctx.overflow_page);
    ASSERT_EQ(1, ntrunc);
    ASSERT_EQ(0x1000, trunc_size_seen);
    ASSERT_EQ(0x1000, H->length);
}

TEST(grow_fails)
{
    status_$t st = 9;
    setup(0x800, 2);
    trunc_status = 0x00030003;
    dir_$alloc_overflow_page(&ctx, &st);
    ASSERT_EQ(0x00030003, st);
    ASSERT_EQ(0x800, H->length);
    ASSERT_EQ(0, npurify);
}

TEST(purify_fails)
{
    status_$t st = 9;
    setup(0x1000, 1);
    purify_status = 0x00050005;
    dir_$alloc_overflow_page(&ctx, &st);
    ASSERT_EQ(0x00050005, st);
    ASSERT_EQ(1, ninval);
    ASSERT_EQ(1, inval_page_seen);
}

int main(void)
{
    printf("dir_$alloc_overflow_page tests\n");
    RUN_TEST(reuses_room);
    RUN_TEST(grows_object);
    RUN_TEST(grow_fails);
    RUN_TEST(purify_fails);
    TEST_SUMMARY();
}
