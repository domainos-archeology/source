/*
 * os/test/test_install_display_aste.c - unit tests for
 * OS_$INSTALL_DISPLAY_ASTE (0x00E6D29A)
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <setjmp.h>

static int tests_passed = 0;
static int tests_failed = 0;

static void reset_state(void);

#define RUN_TEST(name) do { \
    printf("  Running %s... ", #name); \
    reset_state(); \
    test_##name(); \
    printf("PASSED\n"); \
    tests_passed++; \
} while (0)

#define ASSERT_EQ(expected, actual) do { \
    if ((unsigned long)(expected) != (unsigned long)(actual)) { \
        printf("FAILED\n    Expected: 0x%lx, Got: 0x%lx at line %d\n", \
               (unsigned long)(expected), (unsigned long)(actual), __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

#include "os/os_internal.h"
#include "node/node.h"

uint32_t NODE_$ME;
uid_t ACL_$FNDWRX = { 0xAC1, 0x2 };

/* ---- mocks ---- */
static jmp_buf crash_jmp;
static int n_crash, n_vtop, n_aote, n_aste, n_assoc, n_segno, n_map, n_touch;
static status_$t crash_status, assoc_fail_at_status;
static int assoc_fail_at;
static uint16_t segno_ret, segno_dflt;
static os_$init_vtoce_t got_vtoce;
static vtoc_$lookup_req_t got_loc;
static uint16_t aste_segs[8];
static uint16_t assoc_page[64];
static uint32_t assoc_ppn[64];
static uint16_t assoc_f1, assoc_f2;
static uint32_t map_va, map_size, map_flags, map_desc;
static uint32_t touch_va[64];
static aste_t astes[8];

void CRASH_SYSTEM(const status_$t *s) { n_crash++; crash_status = *s; longjmp(crash_jmp, 1); }
uint32_t VTOP_OR_CRASH(uint32_t *va_p) { n_vtop++; return (*va_p >> 10) + 0x100; }
void AST_$ACTIVATE_AOTE_CANNED(uint32_t *attrs, uint32_t *obj_info)
{
    n_aote++;
    memcpy(&got_vtoce, attrs, sizeof(got_vtoce));
    memcpy(&got_loc, obj_info, sizeof(got_loc));
}
aste_t *AST_$ACTIVATE_ASTE_CANNED(uid_t *uid, uint16_t seg)
{
    (void)uid; aste_segs[n_aste] = seg; return &astes[n_aste++];
}
void AST_$PMAP_ASSOC(aste_t *aste, uint16_t page, uint32_t ppn, uint16_t flags1,
                     uint16_t flags2, status_$t *status)
{
    (void)aste;
    assoc_page[n_assoc] = page; assoc_ppn[n_assoc] = ppn;
    assoc_f1 = flags1; assoc_f2 = flags2;
    *status = (n_assoc == assoc_fail_at) ? assoc_fail_at_status : 0;
    n_assoc++;
}
uint16_t MST_$VA_TO_SEGNO(uint32_t va, uint16_t *segno_out, uint16_t dflt)
{
    (void)va; (void)segno_out; n_segno++; segno_dflt = dflt; return segno_ret;
}
void MST_$MAP_CANNED_AT(uint32_t va, uid_t *uid, uint32_t offset,
                        uint32_t size, uint32_t flags, boolean wire,
                        boolean touch, uint32_t desc, status_$t *status)
{
    (void)uid; (void)offset; (void)wire; (void)touch;
    n_map++; map_va = va; map_size = size; map_flags = flags; map_desc = desc;
    *status = 0;
}
uint32_t MST_$TOUCH(uint32_t va, status_$t *status, int16_t wire)
{
    (void)wire; touch_va[n_touch++] = va; *status = 0; return 0;
}

#include "../install_display_aste.c"

static uid_t disp_uid = { 0x00D15, 0x1 };

static void reset_state(void)
{
    n_crash = n_vtop = n_aote = n_aste = n_assoc = n_segno = n_map = n_touch = 0;
    assoc_fail_at = -1;
    segno_ret = 0x20;
    NODE_$ME = 0x4321;
}

static void test_no_touch_64k(void)
{
    uint32_t va = 0x00FC0000;
    int32_t size = 0x10000;
    int8_t touch = 0;
    OS_$INSTALL_DISPLAY_ASTE(&disp_uid, &va, &size, &touch);
    ASSERT_EQ(1, n_vtop);
    ASSERT_EQ(1, n_aote);
    ASSERT_EQ(0xD15, got_vtoce.file_uid.high);
    ASSERT_EQ(0xAC1, got_vtoce.acl_uid.high);
    ASSERT_EQ(0x10000, got_vtoce.length);
    ASSERT_EQ(0, got_vtoce.field74);
    ASSERT_EQ(0xD15, got_loc.uid.high);
    ASSERT_EQ(0x4321, got_loc.node);
    ASSERT_EQ(0, got_loc.flags);
    ASSERT_EQ(2, n_aste);
    ASSERT_EQ(1, aste_segs[1]);
    ASSERT_EQ(64, n_assoc);
    ASSERT_EQ(31, assoc_page[31]);
    ASSERT_EQ(0, assoc_page[32]);
    ASSERT_EQ(0x4000, assoc_ppn[0]);
    ASSERT_EQ(0x4000 + 63, assoc_ppn[63]);
    ASSERT_EQ(0x00FF, assoc_f1);
    ASSERT_EQ(0, assoc_f2);
    ASSERT_EQ(0, n_map);
    ASSERT_EQ(0, n_touch);
}

static void test_partial_segment_and_touch(void)
{
    uint32_t va = 0x00FC0000;
    int32_t size = 0x8000 + 0x401;      /* 2 segments, 34 pages */
    int8_t touch = (int8_t)0xFF;
    OS_$INSTALL_DISPLAY_ASTE(&disp_uid, &va, &size, &touch);
    ASSERT_EQ(2, n_aste);
    ASSERT_EQ(34, n_assoc);
    ASSERT_EQ(1, n_segno);
    ASSERT_EQ(0x3A, segno_dflt);
    ASSERT_EQ(1, n_map);
    ASSERT_EQ(0x00FC0000, map_va);
    ASSERT_EQ(0x8401, map_size);
    ASSERT_EQ(0x00160001, map_flags);
    ASSERT_EQ(0, map_desc);
    ASSERT_EQ(34, n_touch);
    ASSERT_EQ(0x00FC0000 + 33 * 0x400, touch_va[33]);
    ASSERT_EQ(0x00FC0000, va);          /* the caller's cell is untouched */
}

static void test_bad_segment_crashes(void)
{
    uint32_t va = 0x00FC0000;
    int32_t size = 0x400;
    int8_t touch = (int8_t)0xFF;
    segno_ret = 0x3A;
    if (setjmp(crash_jmp) == 0) {
        OS_$INSTALL_DISPLAY_ASTE(&disp_uid, &va, &size, &touch);
    }
    ASSERT_EQ(1, n_crash);
    ASSERT_EQ(0x00040004, crash_status);
    ASSERT_EQ(0, n_map);
}

static void test_assoc_failure_crashes(void)
{
    uint32_t va = 0x00FC0000;
    int32_t size = 0x800;
    int8_t touch = 0;
    assoc_fail_at = 1;
    assoc_fail_at_status = 0x00030005;
    if (setjmp(crash_jmp) == 0) {
        OS_$INSTALL_DISPLAY_ASTE(&disp_uid, &va, &size, &touch);
    }
    ASSERT_EQ(1, n_crash);
    ASSERT_EQ(0x00030005, crash_status);
    ASSERT_EQ(2, n_assoc);
}

int main(void)
{
    printf("OS_$INSTALL_DISPLAY_ASTE tests\n");
    RUN_TEST(no_touch_64k);
    RUN_TEST(partial_segment_and_touch);
    RUN_TEST(bad_segment_crashes);
    RUN_TEST(assoc_failure_crashes);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
