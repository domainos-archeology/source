/*
 * mst/test/test_remap_privi.c - unit tests for MST_$REMAP_PRIVI (0x00E43A0C).
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;
static int current_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do {                     \
    printf("  Running %s... ", #name);          \
    current_failed = 0;                         \
    test_##name();                              \
    if (current_failed) { tests_failed++; }     \
    else { tests_passed++; printf("PASSED\n"); }\
} while (0)

#define ASSERT_EQ(expected, actual) do {                                 \
    unsigned long long _e = (unsigned long long)(expected);              \
    unsigned long long _a = (unsigned long long)(actual);                \
    if (_e != _a) {                                                      \
        printf("FAILED\n    Expected 0x%llx, got 0x%llx at line %d\n",   \
               _e, _a, __LINE__);                                        \
        current_failed = 1;                                              \
        return;                                                          \
    }                                                                    \
} while (0)

#include "mst/mst_internal.h"
#include "acl/acl.h"

uint16_t PROC1_$AS_ID = 5;
uint16_t MST_$SEG_GLOBAL_B = 0x100;
static mste_t the_mste;

static status_$t pte_status, unmap_status, alloc_status;
static int n_unmap, n_alloc, n_enter, n_exit, super_at_alloc;
static int16_t unmap_mode;
static uint16_t unmap_asid;
static uint32_t unmap_start, unmap_size;
static uid_t unmap_uid, alloc_uid;
static uint32_t alloc_hint, alloc_start, alloc_len, alloc_area_size;
static int16_t alloc_asid;
static uint16_t alloc_area_id, alloc_touch;
static uint8_t alloc_access;
static boolean alloc_dir;
static void *alloc_map_info;
static char alloc_ret;

void mst_$va_to_pte(uint16_t asid, uint32_t va, uint16_t *prot, void **entry,
                    status_$t *status)
{
    (void)asid; (void)va;
    *prot = 0x2A;
    *entry = &the_mste;
    *status = pte_status;
}
void MST_$UNMAP_PRIVI(int16_t mode, uid_t *uid, uint32_t start, uint32_t size,
                      uint16_t asid, status_$t *status)
{
    n_unmap++; unmap_mode = mode; unmap_uid = *uid; unmap_start = start;
    unmap_size = size; unmap_asid = asid;
    *status = unmap_status;
}
void ACL_$ENTER_SUPER(void) { n_enter++; }
void ACL_$EXIT_SUPER(void) { n_exit++; }
void *mst_$alloc_segs(uint32_t addr_hint, uid_t *uid, uint32_t start_va, uint32_t length,
                      uint32_t area_size, int16_t asid, uint16_t area_id, uint16_t touch_count,
                      uint8_t access_rights, boolean direction, void *map_info,
                      status_$t *status)
{
    n_alloc++; super_at_alloc = n_enter - n_exit;
    alloc_hint = addr_hint; alloc_uid = *uid; alloc_start = start_va; alloc_len = length;
    alloc_area_size = area_size; alloc_asid = asid; alloc_area_id = area_id;
    alloc_touch = touch_count; alloc_access = access_rights; alloc_dir = direction;
    alloc_map_info = map_info;
    *status = alloc_status;
    return &alloc_ret;
}

#include "../remap_privi.c"

static void reset(void)
{
    pte_status = unmap_status = alloc_status = 0;
    n_unmap = n_alloc = n_enter = n_exit = 0;
    the_mste.uid.high = 0x11112222u;
    the_mste.uid.low = 0x33334444u;
    the_mste.unknown_0a = 0x8000;
    the_mste.location = 0x14000000u;    /* byte +0xC = 0x14: (0x14 & 0x7C) >> 2 = 5 */
}

TEST(user_remap)
{
    uint16_t flags = 0;
    uint32_t va = 0x00100000u, ulen = 0x8000, off = 0x18000, len = 0x10000;
    uint32_t info = 0xDEAD;
    status_$t st;
    void *r;

    reset();
    r = MST_$REMAP_PRIVI(&flags, &va, &ulen, &off, &len, &info, &st);
    ASSERT_EQ((uintptr_t)&alloc_ret, (uintptr_t)r);
    ASSERT_EQ(0, info);
    ASSERT_EQ(2, unmap_mode);
    ASSERT_EQ(5, unmap_asid);
    ASSERT_EQ(0x00100000u, unmap_start);
    ASSERT_EQ(0x8000, unmap_size);
    ASSERT_EQ(0x33334444u, unmap_uid.low);
    ASSERT_EQ(1, super_at_alloc);
    ASSERT_EQ(1, n_exit);
    ASSERT_EQ(0x7FFFFFFFu, alloc_hint);
    ASSERT_EQ(0x11112222u, alloc_uid.high);
    ASSERT_EQ(0x18000, alloc_start);
    ASSERT_EQ(0x10000, alloc_len);
    ASSERT_EQ(0, alloc_area_size);
    ASSERT_EQ(5, alloc_asid);
    ASSERT_EQ(0x2A, alloc_area_id);
    ASSERT_EQ(6, alloc_touch);
    ASSERT_EQ(0xFF, alloc_access);
    ASSERT_EQ(0, (uint8_t)alloc_dir);
    ASSERT_EQ((uintptr_t)&info, (uintptr_t)alloc_map_info);
}

/* flags bit 0: mode 3; ASID 0 at or above segment MST_$SEG_GLOBAL_B. */
TEST(privileged_global_and_private)
{
    uint16_t flags = 1;
    uint32_t va = 0x100u << 15, ulen = 1, off = 0, len = 1, info;
    status_$t st;

    reset();
    the_mste.unknown_0a = 0x7FFF;
    MST_$REMAP_PRIVI(&flags, &va, &ulen, &off, &len, &info, &st);
    ASSERT_EQ(3, unmap_mode);
    ASSERT_EQ(5, unmap_asid);
    ASSERT_EQ(0, alloc_asid);
    ASSERT_EQ(0xFF, (uint8_t)alloc_dir);
    ASSERT_EQ(0, alloc_access);

    va = (0x100u << 15) - 1;
    MST_$REMAP_PRIVI(&flags, &va, &ulen, &off, &len, &info, &st);
    ASSERT_EQ(5, alloc_asid);
}

/* Only the low status word is tested after mst_$va_to_pte. */
TEST(pte_errors)
{
    uint16_t flags = 0;
    uint32_t va = 0, ulen = 0, off = 0, len = 0, info = 9;
    status_$t st;
    void *r;

    reset();
    pte_status = 0x00130004;
    r = MST_$REMAP_PRIVI(&flags, &va, &ulen, &off, &len, &info, &st);
    ASSERT_EQ(0, (uintptr_t)r);
    ASSERT_EQ(0, info);
    ASSERT_EQ(0, n_unmap);

    reset();
    pte_status = 0x00130000;             /* low word zero: carries on */
    r = MST_$REMAP_PRIVI(&flags, &va, &ulen, &off, &len, &info, &st);
    ASSERT_EQ(1, n_unmap);
    ASSERT_EQ((uintptr_t)&alloc_ret, (uintptr_t)r);
}

TEST(unmap_error)
{
    uint16_t flags = 0;
    uint32_t va = 0, ulen = 0, off = 0, len = 0, info;
    status_$t st;
    void *r;

    reset();
    unmap_status = 0x00130001;
    r = MST_$REMAP_PRIVI(&flags, &va, &ulen, &off, &len, &info, &st);
    ASSERT_EQ(0, (uintptr_t)r);
    ASSERT_EQ(0x00130001, st);
    ASSERT_EQ(0, n_alloc);
    ASSERT_EQ(0, n_enter);
}

int main(void)
{
    printf("MST_$REMAP_PRIVI tests\n");
    RUN_TEST(user_remap);
    RUN_TEST(privileged_global_and_private);
    RUN_TEST(pte_errors);
    RUN_TEST(unmap_error);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
