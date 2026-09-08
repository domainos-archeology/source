/*
 * acl/test/test_init.c - unit tests for ACL_$INIT (0x00E3109C).
 *
 * acl/init.c is #included below with OS_$DATA_ZERO, ACL_$FREE_ASID and
 * ML_$EXCLUSION_INIT mocked.  The tests pin down what bead source-v9xc found
 * missing or wrong: the zeroed extent, the LOGIN SID slots the locksmith UID
 * really lands in, the eight UID_$NIL project slots, and the 31-entry
 * circular free-list ring.
 */

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

#define ASSERT_TRUE(cond) do {                                           \
    if (!(cond)) {                                                       \
        printf("FAILED\n    %s at line %d\n", #cond, __LINE__);          \
        current_failed = 1;                                              \
        return;                                                          \
    }                                                                    \
} while (0)

#include "acl/acl_internal.h"

/* ------------------------------------------------------------------ */
/* Globals                                                             */
/* ------------------------------------------------------------------ */

/* aligned(2) keeps the host linker from giving this 31KB common symbol a
 * 32KB alignment, which macOS ld warns about. */
acl_$cache_slot_t ACL_$ACL_CACHE[ACL_CACHE_SLOTS] __attribute__((aligned(2)));
acl_$cache_link_t ACL_$CACHE_HASH_LINKS[ACL_CACHE_LINK_SLOTS];
acl_sid_block_t   ACL_$ORIGINAL_SIDS[PROC1_MAX_PROCESSES];
acl_sid_block_t   ACL_$CURRENT_SIDS[PROC1_MAX_PROCESSES];
uid_t             ACL_$PROJ_UIDS[PROC1_MAX_PROCESSES][ACL_MAX_PROJECTS];
uint8_t           ACL_$ASID_FREE_BITMAP[8];
ml_$exclusion_t   ACL_$EXCLUSION_LOCK;

uid_t UID_$NIL              = { 0, 0 };
uid_t RGYC_$G_LOCKSMITH_UID = { 0x00000542u, 0x00000000u };  /* 0x00E17434 */

/* ------------------------------------------------------------------ */
/* Mocks                                                               */
/* ------------------------------------------------------------------ */

static const void *zero_base;
static uint32_t    zero_len;
static int         zero_calls;

void OS_$DATA_ZERO(void *ptr, uint32_t len)
{
    /* Deliberately does NOT write: the image extent is larger than the
     * individual C objects the host test can declare. */
    zero_calls++;
    zero_base = ptr;
    zero_len  = len;
}

static int      free_asid_calls;
static int16_t  free_asid_first;
static int16_t  free_asid_last;
static int      free_asid_status_ok;

void ACL_$FREE_ASID(int16_t asid, status_$t *status_ret)
{
    if (free_asid_calls == 0) {
        free_asid_first = asid;
    }
    free_asid_last = asid;
    free_asid_calls++;
    free_asid_status_ok = (status_ret != NULL);
    if (status_ret != NULL) {
        *status_ret = status_$ok;
    }
}

static int excl_init_calls;
static const ml_$exclusion_t *excl_init_arg;

void ML_$EXCLUSION_INIT(ml_$exclusion_t *excl)
{
    excl_init_calls++;
    excl_init_arg = excl;
}

#include "../init.c"

static void run_init(void)
{
    memset(ACL_$CACHE_HASH_LINKS, 0xA5, sizeof(ACL_$CACHE_HASH_LINKS));
    memset(ACL_$ORIGINAL_SIDS, 0, sizeof(ACL_$ORIGINAL_SIDS));
    memset(ACL_$CURRENT_SIDS, 0, sizeof(ACL_$CURRENT_SIDS));
    memset(ACL_$PROJ_UIDS, 0x5A, sizeof(ACL_$PROJ_UIDS));
    memset(ACL_$ASID_FREE_BITMAP, 0, sizeof(ACL_$ASID_FREE_BITMAP));
    zero_calls = 0;
    zero_base = NULL;
    zero_len = 0;
    free_asid_calls = 0;
    free_asid_first = free_asid_last = -1;
    free_asid_status_ok = 0;
    excl_init_calls = 0;
    excl_init_arg = NULL;

    ACL_$INIT();
}

/* 0x00E310AA-0x00E310C6: OS_$DATA_ZERO(0xE88834, 0xE935CC - 0xE88834). */
TEST(zeroes_the_whole_acl_data_segment)
{
    run_init();
    ASSERT_EQ(1, zero_calls);
    ASSERT_TRUE(zero_base == (const void *)&ACL_$ACL_CACHE[0]);
    ASSERT_EQ(0xAD98u, zero_len);
    ASSERT_EQ(0xAD98u, ACL_DATA_SIZE);
}

/* 0x00E310C8-0x00E31102: `moveq #0x3f,D2` + `dbf` is 64 turns, D3 = 1..64. */
TEST(frees_asids_one_through_sixty_four)
{
    int i;

    run_init();
    ASSERT_EQ(64, free_asid_calls);
    ASSERT_EQ(1, free_asid_first);
    ASSERT_EQ(64, free_asid_last);
    ASSERT_EQ(1, free_asid_status_ok);

    /* Every bit of the 64-bit free bitmap is set. */
    for (i = 0; i < 8; i++) {
        ASSERT_EQ(0xFF, ACL_$ASID_FREE_BITMAP[i]);
    }
}

/*
 * 0x00E31106-0x00E3111E: destinations 0xE9044C and 0xE90D4C, i.e. offset
 * 0x3C = 1*0x24 + 0x18 - process 1's LOGIN SID, in both tables.
 */
TEST(locksmith_uid_lands_in_process_one_login_sid)
{
    run_init();

    ASSERT_EQ(0x00000542u, ACL_$ORIGINAL_SIDS[1].login_sid.high);
    ASSERT_EQ(0x00000000u, ACL_$ORIGINAL_SIDS[1].login_sid.low);
    ASSERT_EQ(0x00000542u, ACL_$CURRENT_SIDS[1].login_sid.high);

    /* Not the user SID, and not process 0. */
    ASSERT_EQ(0u, ACL_$ORIGINAL_SIDS[1].user_sid.high);
    ASSERT_EQ(0u, ACL_$CURRENT_SIDS[1].user_sid.high);
    ASSERT_EQ(0u, ACL_$ORIGINAL_SIDS[0].login_sid.high);
    ASSERT_EQ(0u, ACL_$CURRENT_SIDS[0].login_sid.high);

    /* Offset 0x3C into the table is exactly &table[1].login_sid. */
    ASSERT_EQ(0x3C, (const uint8_t *)&ACL_$ORIGINAL_SIDS[1].login_sid -
                    (const uint8_t *)&ACL_$ORIGINAL_SIDS[0]);
}

/* 0x00E31122-0x00E3113C: eight UID_$NIL to 0xE9253C + k*8. */
TEST(project_uid_row_one_is_nil)
{
    int k;

    run_init();
    for (k = 0; k < 8; k++) {
        ASSERT_EQ(0u, ACL_$PROJ_UIDS[1][k].high);
        ASSERT_EQ(0u, ACL_$PROJ_UIDS[1][k].low);
    }
    /* Row 0 and row 2 were left alone. */
    ASSERT_EQ(0x5A5A5A5Au, ACL_$PROJ_UIDS[0][0].high);
    ASSERT_EQ(0x5A5A5A5Au, ACL_$PROJ_UIDS[2][0].high);

    /* 0xE9253C - 0xE924FC = 0x40 = one row. */
    ASSERT_EQ(0x40, (const uint8_t *)&ACL_$PROJ_UIDS[1][0] -
                    (const uint8_t *)&ACL_$PROJ_UIDS[0][0]);
}

/* 0x00E31140-0x00E3117A: 31 entries, next = (i+1) mod 31, prev = (i+30) mod 31. */
TEST(free_list_ring)
{
    int i;

    run_init();
    for (i = 0; i <= 30; i++) {
        ASSERT_EQ((i + 1) % 31, ACL_$CACHE_HASH_LINKS[i].next);
        ASSERT_EQ((i + 30) % 31, ACL_$CACHE_HASH_LINKS[i].prev);
    }

    /* The ring closes: entry 30 points back at 0, entry 0 back at 30. */
    ASSERT_EQ(0, ACL_$CACHE_HASH_LINKS[30].next);
    ASSERT_EQ(30, ACL_$CACHE_HASH_LINKS[0].prev);

    /* Entry 31 (the 32nd link slot) is not part of the ring. */
    ASSERT_EQ((int16_t)0xA5A5, ACL_$CACHE_HASH_LINKS[31].next);
}

/* 0x00E3117E-0x00E31188 */
TEST(initialises_the_exclusion_lock)
{
    run_init();
    ASSERT_EQ(1, excl_init_calls);
    ASSERT_TRUE(excl_init_arg == &ACL_$EXCLUSION_LOCK);
}

int main(void)
{
    printf("ACL_$INIT tests\n");

    RUN_TEST(zeroes_the_whole_acl_data_segment);
    RUN_TEST(frees_asids_one_through_sixty_four);
    RUN_TEST(locksmith_uid_lands_in_process_one_login_sid);
    RUN_TEST(project_uid_row_one_is_nil);
    RUN_TEST(free_list_ring);
    RUN_TEST(initialises_the_exclusion_lock);

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
