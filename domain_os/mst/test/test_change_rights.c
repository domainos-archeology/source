/*
 * mst/test/test_change_rights.c - unit tests for MST_$CHANGE_RIGHTS (0x00E42FB0).
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
static mste_t the_mste;

static status_$t pte_status, unmap_status, alloc_status[2];
static int n_unmap, n_alloc, n_enter, n_exit, super_at[2];
static int16_t unmap_mode;
static uint32_t unmap_start, unmap_size;
static uint32_t a_hint[2], a_start[2], a_len[2];
static uint16_t a_area[2], a_touch[2];
static uint8_t a_access[2];
static boolean a_dir[2];
static status_$t *a_status[2];

void mst_$va_to_pte(uint16_t asid, uint32_t va, uint16_t *prot, void **entry,
                    status_$t *status)
{
    (void)asid; (void)va;
    *prot = 0x0033;
    *entry = &the_mste;
    *status = pte_status;
}
void MST_$UNMAP_PRIVI(int16_t mode, uid_t *uid, uint32_t start, uint32_t size,
                      uint16_t asid, status_$t *status)
{
    (void)uid; (void)asid;
    n_unmap++; unmap_mode = mode; unmap_start = start; unmap_size = size;
    *status = unmap_status;
}
void ACL_$ENTER_SUPER(void) { n_enter++; }
void ACL_$EXIT_SUPER(void) { n_exit++; }
void *mst_$alloc_segs(uint32_t addr_hint, uid_t *uid, uint32_t start_va, uint32_t length,
                      uint32_t area_size, int16_t asid, uint16_t area_id, uint16_t touch_count,
                      uint8_t access_rights, boolean direction, void *map_info,
                      status_$t *status)
{
    int i = n_alloc++;
    (void)uid; (void)area_size; (void)asid; (void)map_info;
    super_at[i] = n_enter - n_exit;
    a_hint[i] = addr_hint; a_start[i] = start_va; a_len[i] = length;
    a_area[i] = area_id; a_touch[i] = touch_count; a_access[i] = access_rights;
    a_dir[i] = direction; a_status[i] = status;
    *status = alloc_status[i];
    return NULL;
}

#include "../change_rights.c"

static uint32_t va, len;
static uint16_t rights;
static status_$t st;

static void reset(void)
{
    pte_status = unmap_status = alloc_status[0] = alloc_status[1] = 0;
    n_unmap = n_alloc = n_enter = n_exit = 0;
    the_mste.uid.high = 1; the_mste.uid.low = 2;
    the_mste.segment = 3;                 /* only bit 0 survives */
    the_mste.unknown_0a = 0;
    the_mste.location = 0x7C000000u;      /* touch = 0x1F + 1 */
    va = 0x00123456u; len = 0x8000; rights = 0x0011;
}

TEST(success)
{
    reset();
    MST_$CHANGE_RIGHTS(&va, &len, &rights, &st);
    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(2, unmap_mode);
    ASSERT_EQ(0x00123456u, unmap_start);
    ASSERT_EQ(0x8000, unmap_size);
    ASSERT_EQ(1, n_alloc);
    ASSERT_EQ(0x00123456u, a_hint[0]);
    ASSERT_EQ(0x8000u + 0x3456u, a_start[0]);   /* (3 & 1) << 15 + (va & 0x7FFF) */
    ASSERT_EQ(0x11, a_area[0]);
    ASSERT_EQ(0x20, a_touch[0]);
    ASSERT_EQ(0, a_access[0]);
    ASSERT_EQ(0, (uint8_t)a_dir[0]);
    ASSERT_EQ(0, n_enter);
}

TEST(segment_bit0_only)
{
    reset();
    the_mste.segment = 4;
    the_mste.unknown_0a = 0x8001;
    MST_$CHANGE_RIGHTS(&va, &len, &rights, &st);
    ASSERT_EQ(0x3456u, a_start[0]);
    ASSERT_EQ(0xFF, a_access[0]);
}

TEST(map_fails_restores_old_rights)
{
    reset();
    alloc_status[0] = 0x00130009;
    alloc_status[1] = 0x00130055;
    MST_$CHANGE_RIGHTS(&va, &len, &rights, &st);
    ASSERT_EQ(2, n_alloc);
    ASSERT_EQ(0x33, a_area[1]);
    ASSERT_EQ(1, super_at[1]);
    ASSERT_EQ(1, n_exit);
    ASSERT_EQ(0x00130009, st);                   /* restore status is local */
    ASSERT_EQ(1, (uintptr_t)a_status[1] != (uintptr_t)&st);
}

TEST(unmap_fails_still_restores)
{
    reset();
    unmap_status = 0x00130001;
    MST_$CHANGE_RIGHTS(&va, &len, &rights, &st);
    ASSERT_EQ(1, n_alloc);
    ASSERT_EQ(0x33, a_area[0]);
    ASSERT_EQ(1, super_at[0]);
    ASSERT_EQ(0x00130001, st);
}

TEST(pte_low_word)
{
    reset();
    pte_status = 0x00130004;
    MST_$CHANGE_RIGHTS(&va, &len, &rights, &st);
    ASSERT_EQ(0, n_unmap);
    reset();
    pte_status = 0x00130000;
    MST_$CHANGE_RIGHTS(&va, &len, &rights, &st);
    ASSERT_EQ(1, n_unmap);
}

int main(void)
{
    printf("MST_$CHANGE_RIGHTS tests\n");
    RUN_TEST(success);
    RUN_TEST(segment_bit0_only);
    RUN_TEST(map_fails_restores_old_rights);
    RUN_TEST(unmap_fails_still_restores);
    RUN_TEST(pte_low_word);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
