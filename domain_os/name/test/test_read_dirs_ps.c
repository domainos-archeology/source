/*
 * name/test/test_read_dirs_ps.c - NAME_$READ_DIRS_PS (0x00E588BE)
 *
 * Pins: UID_$NIL means PROC1_$AS_ID (and AS_ID 0 is "not found"); other
 * UIDs are looked up in PROC2_$UID[1..57] only (index 0 never matches);
 * the answers are NAME_$WDIR_UID[asid] and NAME_$NDIR_UID[asid].
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

#include "name/name_internal.h"
#include "proc2/proc2.h"

name_$data_t NAME_$DATA;
MODULE_DATA_DEFINE(proc2_$unwired_data_t, PROC2_$UNWIRED_DATA, 0x00E7BE84);
uid_t UID_$NIL;
uint16_t PROC1_$AS_ID;

#include "../read_dirs_ps.c"

static uid_t w, n;
static status_$t st;

static void setup(void)
{
    int i;
    memset(&NAME_$DATA, 0, sizeof(NAME_$DATA));
    memset(&PROC2_$UNWIRED_DATA, 0, sizeof(PROC2_$UNWIRED_DATA));
    for (i = 0; i < 58; i++) {
        NAME_$DATA.wdir_uid[i].high = 0x1000 + i;
        NAME_$DATA.ndir_uid[i].high = 0x2000 + i;
        PROC2_$UNWIRED_DATA.uid[i].high = 0x500;
        PROC2_$UNWIRED_DATA.uid[i].low = (uint32_t)i;
    }
    w.high = n.high = 0;
    st = 1;
}

TEST(nil_uses_current_asid)
{
    uid_t nil = { 0, 0 };
    setup();
    PROC1_$AS_ID = 7;
    NAME_$READ_DIRS_PS(&nil, &w, &n, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(0x1007, w.high);
    ASSERT_EQ(0x2007, n.high);
}

TEST(nil_with_asid_zero_not_found)
{
    uid_t nil = { 0, 0 };
    setup();
    PROC1_$AS_ID = 0;
    NAME_$READ_DIRS_PS(&nil, &w, &n, &st);
    ASSERT_EQ(status_$proc2_uid_not_found, st);
    ASSERT_EQ(0, w.high);
}

TEST(lookup_found_last_slot)
{
    uid_t p = { 0x500, 57 };
    setup();
    NAME_$READ_DIRS_PS(&p, &w, &n, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(0x1000 + 57, w.high);
    ASSERT_EQ(0x2000 + 57, n.high);
}

TEST(slot_zero_never_matches)
{
    uid_t p = { 0x500, 0 };
    setup();
    NAME_$READ_DIRS_PS(&p, &w, &n, &st);
    ASSERT_EQ(status_$proc2_uid_not_found, st);
}

int main(void)
{
    printf("NAME_$READ_DIRS_PS tests:\n");
    RUN_TEST(nil_uses_current_asid);
    RUN_TEST(nil_with_asid_zero_not_found);
    RUN_TEST(lookup_found_last_slot);
    RUN_TEST(slot_zero_never_matches);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
