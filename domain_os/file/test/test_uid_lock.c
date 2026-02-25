/*
 * file/test/test_uid_lock.c - Unit tests for FILE_$UID_LOCK_ACQUIRE/RELEASE
 *
 * Tests the per-UID hash-bucket locking mechanism used by file delete.
 * Since we can't run full kernel code, we mock the ML and EC functions
 * and test the hash computation and lock state management.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

/* ============================================================================
 * Test framework
 * ============================================================================ */

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  Running %s... ", #name); \
    test_##name(); \
    tests_passed++; \
    printf("PASSED\n"); \
} while(0)

#define ASSERT_EQ(expected, actual) do { \
    if ((expected) != (actual)) { \
        printf("FAILED\n    Expected: 0x%lx, Got: 0x%lx at line %d\n", \
               (unsigned long)(expected), (unsigned long)(actual), __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_NE(not_expected, actual) do { \
    if ((not_expected) == (actual)) { \
        printf("FAILED\n    Did not expect: 0x%lx at line %d\n", \
               (unsigned long)(not_expected), __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

/* ============================================================================
 * Minimal type stubs for building outside the kernel
 * ============================================================================ */

typedef long status_$t;
#define status_$ok 0

typedef struct uid_t {
    uint32_t high;
    uint32_t low;
} uid_t;

typedef struct ec_$eventcount_t {
    union {
        int32_t value;
        int32_t count;
    };
    void *waiter_list_head;
    void *waiter_list_tail;
} ec_$eventcount_t;

/* ============================================================================
 * Globals that the code under test accesses
 * ============================================================================ */

#define FILE_UID_LOCK_BUCKETS 17
uint8_t FILE_$UID_LOCK_HOLDERS[FILE_UID_LOCK_BUCKETS];
ec_$eventcount_t FILE_$UID_LOCK_EC;
uint16_t PROC1_$CURRENT = 0x0007;  /* PID = 7 for testing */

/* ============================================================================
 * Mock tracking
 * ============================================================================ */

static int mock_ml_lock_count = 0;
static int mock_ml_unlock_count = 0;
static int16_t mock_ml_lock_id = -1;
static int16_t mock_ml_unlock_id = -1;
static int mock_ec_waitn_count = 0;
static int mock_ec_advance_count = 0;

static void reset_mocks(void) {
    memset(FILE_$UID_LOCK_HOLDERS, 0, sizeof(FILE_$UID_LOCK_HOLDERS));
    memset(&FILE_$UID_LOCK_EC, 0, sizeof(FILE_$UID_LOCK_EC));
    mock_ml_lock_count = 0;
    mock_ml_unlock_count = 0;
    mock_ml_lock_id = -1;
    mock_ml_unlock_id = -1;
    mock_ec_waitn_count = 0;
    mock_ec_advance_count = 0;
    PROC1_$CURRENT = 0x0007;
}

/* ============================================================================
 * Mock implementations
 * ============================================================================ */

void ML_$LOCK(int16_t resource_id) {
    mock_ml_lock_count++;
    mock_ml_lock_id = resource_id;
}

void ML_$UNLOCK(int16_t resource_id) {
    mock_ml_unlock_count++;
    mock_ml_unlock_id = resource_id;
}

uint16_t EC_$WAITN(ec_$eventcount_t **ecs, int32_t *wait_val, int16_t num_ecs) {
    mock_ec_waitn_count++;
    /*
     * In a real test of the busy-wait, the mock would need to clear the
     * holder slot to break the loop. For unit testing, we avoid calling
     * the function when the slot is busy.
     */
    (void)ecs;
    (void)wait_val;
    (void)num_ecs;
    return 0;
}

void EC_$ADVANCE(ec_$eventcount_t *ec) {
    mock_ec_advance_count++;
    (void)ec;
}

/* ============================================================================
 * Inline the UID hash computation for testing
 *
 * The hash is:
 *   temp = uid->high ^ uid->low
 *   hash = ((temp & 0xFFFF) ^ ((temp >> 16) & 0xFFFF)) % 17
 * ============================================================================ */

static uint16_t uid_lock_hash(uid_t *uid) {
    uint32_t temp = uid->high ^ uid->low;
    uint16_t folded = (uint16_t)(temp & 0xFFFF) ^ (uint16_t)((temp >> 16) & 0xFFFF);
    return folded % FILE_UID_LOCK_BUCKETS;
}

/*
 * Pull in the actual implementations.
 * We pre-define header guards to prevent the kernel headers from
 * being included -- all needed types and globals are provided above
 * as mock definitions. This allows the test to build with the host
 * compiler without m68k-specific headers.
 */
#define BASE_H           /* Skip base.h - we have our own types */
#define EC_H             /* Skip ec.h - ec_$eventcount_t defined above */
#define ML_H             /* Skip ml.h - ML_$LOCK/UNLOCK mocked above */
#define UID_H            /* Skip uid.h - uid_t defined above */
#define FILE_H           /* Skip file.h - types provided above */
#define FILE_INTERNAL_H  /* Skip file_internal.h - types provided above */
#define PROC1_H          /* Skip proc1.h */
#define AST_H            /* Skip ast.h */
#define ACL_H            /* Skip acl.h */
#define TIME_H           /* Skip time.h */
#define HINT_H           /* Skip hint.h */
#define REM_FILE_H       /* Skip rem_file.h */
#define AUDIT_H          /* Skip audit.h */
#define NETWORK_H        /* Skip network.h */
#define ROUTE_H          /* Skip route.h */
#define DISK_H           /* Skip disk.h */
#define VTOC_H           /* Skip vtoc.h */
#define PROC1_CONFIG_H   /* Skip proc1_config.h */
#define ARCH_H           /* Skip arch.h */
#include "../uid_lock_acquire.c"
#include "../uid_lock_release.c"

/* ============================================================================
 * Tests: Hash computation
 * ============================================================================ */

/*
 * Test: Hash of zero UID should be 0
 * (0 ^ 0) = 0, (0 ^ 0) = 0, 0 % 17 = 0
 */
TEST(hash_zero_uid) {
    uid_t uid = { 0x00000000, 0x00000000 };
    ASSERT_EQ(0, uid_lock_hash(&uid));
}

/*
 * Test: Hash produces values in range [0, 16]
 */
TEST(hash_range) {
    uid_t uid;
    for (uint32_t i = 0; i < 1000; i++) {
        uid.high = i * 0x12345;
        uid.low = i * 0x67890;
        uint16_t h = uid_lock_hash(&uid);
        if (h >= FILE_UID_LOCK_BUCKETS) {
            printf("FAILED\n    hash(%u) = %u, out of range\n", i, h);
            tests_failed++;
            return;
        }
    }
}

/*
 * Test: Specific known hash value
 * uid = { 0x12345678, 0x9ABCDEF0 }
 * temp = 0x12345678 ^ 0x9ABCDEF0 = 0x88888888
 * folded = 0x8888 ^ 0x8888 = 0x0000
 * hash = 0 % 17 = 0
 */
TEST(hash_known_value_1) {
    uid_t uid = { 0x12345678, 0x9ABCDEF0 };
    ASSERT_EQ(0, uid_lock_hash(&uid));
}

/*
 * Test: Another specific hash value
 * uid = { 0x00010001, 0x00000000 }
 * temp = 0x00010001
 * folded = 0x0001 ^ 0x0001 = 0x0000
 * hash = 0 % 17 = 0
 */
TEST(hash_known_value_2) {
    uid_t uid = { 0x00010001, 0x00000000 };
    ASSERT_EQ(0, uid_lock_hash(&uid));
}

/*
 * Test: Hash that gives non-zero result
 * uid = { 0x00000001, 0x00000000 }
 * temp = 0x00000001
 * folded = 0x0001 ^ 0x0000 = 0x0001
 * hash = 1 % 17 = 1
 */
TEST(hash_known_value_3) {
    uid_t uid = { 0x00000001, 0x00000000 };
    ASSERT_EQ(1, uid_lock_hash(&uid));
}

/*
 * Test: Hash with value that exercises modulo
 * uid = { 0x00110000, 0x00000000 }
 * temp = 0x00110000
 * folded = 0x0000 ^ 0x0011 = 0x0011 = 17
 * hash = 17 % 17 = 0
 */
TEST(hash_modulo_exact) {
    uid_t uid = { 0x00110000, 0x00000000 };
    ASSERT_EQ(0, uid_lock_hash(&uid));
}

/*
 * Test: Hash with value 18 wraps to 1
 * uid = { 0x00120000, 0x00000000 }
 * temp = 0x00120000
 * folded = 0x0000 ^ 0x0012 = 0x0012 = 18
 * hash = 18 % 17 = 1
 */
TEST(hash_modulo_wrap) {
    uid_t uid = { 0x00120000, 0x00000000 };
    ASSERT_EQ(1, uid_lock_hash(&uid));
}

/* ============================================================================
 * Tests: Lock acquire (slot free case)
 * ============================================================================ */

/*
 * Test: Acquire on a free slot succeeds immediately
 * Should NOT call ML_$UNLOCK, EC_$WAITN, or ML_$LOCK
 */
TEST(acquire_free_slot) {
    uid_t uid = { 0x00000001, 0x00000000 };
    uint16_t bucket = uid_lock_hash(&uid);

    reset_mocks();
    ASSERT_EQ(0, FILE_$UID_LOCK_HOLDERS[bucket]);

    FILE_$UID_LOCK_ACQUIRE(&uid);

    /* Should have stored the PID low byte */
    ASSERT_EQ(0x07, FILE_$UID_LOCK_HOLDERS[bucket]);

    /* Should NOT have waited (slot was free) */
    ASSERT_EQ(0, mock_ml_unlock_count);
    ASSERT_EQ(0, mock_ec_waitn_count);
    ASSERT_EQ(0, mock_ml_lock_count);
}

/*
 * Test: Acquire stores correct PID for different PROC1_$CURRENT values
 */
TEST(acquire_stores_pid_low_byte) {
    uid_t uid = { 0x00000001, 0x00000000 };
    uint16_t bucket = uid_lock_hash(&uid);

    reset_mocks();
    PROC1_$CURRENT = 0x002A;  /* PID = 42 */
    FILE_$UID_LOCK_ACQUIRE(&uid);
    ASSERT_EQ(0x2A, FILE_$UID_LOCK_HOLDERS[bucket]);
}

/*
 * Test: Acquire with PID that has high byte set
 * The code only stores the low byte of PROC1_$CURRENT
 */
TEST(acquire_pid_low_byte_only) {
    uid_t uid = { 0x00000001, 0x00000000 };
    uint16_t bucket = uid_lock_hash(&uid);

    reset_mocks();
    PROC1_$CURRENT = 0xFF03;  /* Low byte = 0x03 */
    FILE_$UID_LOCK_ACQUIRE(&uid);
    ASSERT_EQ(0x03, FILE_$UID_LOCK_HOLDERS[bucket]);
}

/* ============================================================================
 * Tests: Lock release
 * ============================================================================ */

/*
 * Test: Release clears the holder byte
 */
TEST(release_clears_holder) {
    uid_t uid = { 0x00000001, 0x00000000 };
    uint16_t bucket = uid_lock_hash(&uid);

    reset_mocks();
    FILE_$UID_LOCK_HOLDERS[bucket] = 0x07;  /* Pre-set as held */

    FILE_$UID_LOCK_RELEASE(&uid);

    ASSERT_EQ(0, FILE_$UID_LOCK_HOLDERS[bucket]);
}

/*
 * Test: Release advances the eventcount
 */
TEST(release_advances_ec) {
    uid_t uid = { 0x00000001, 0x00000000 };

    reset_mocks();
    FILE_$UID_LOCK_HOLDERS[uid_lock_hash(&uid)] = 0x07;

    FILE_$UID_LOCK_RELEASE(&uid);

    ASSERT_EQ(1, mock_ec_advance_count);
}

/*
 * Test: Release only affects the correct bucket
 */
TEST(release_correct_bucket_only) {
    uid_t uid1 = { 0x00000001, 0x00000000 };  /* bucket 1 */
    uid_t uid2 = { 0x00000002, 0x00000000 };  /* bucket 2 */
    uint16_t bucket1 = uid_lock_hash(&uid1);
    uint16_t bucket2 = uid_lock_hash(&uid2);

    reset_mocks();
    FILE_$UID_LOCK_HOLDERS[bucket1] = 0x07;
    FILE_$UID_LOCK_HOLDERS[bucket2] = 0x07;

    FILE_$UID_LOCK_RELEASE(&uid1);

    ASSERT_EQ(0, FILE_$UID_LOCK_HOLDERS[bucket1]);
    ASSERT_EQ(0x07, FILE_$UID_LOCK_HOLDERS[bucket2]);
}

/* ============================================================================
 * Tests: Acquire/Release round trip
 * ============================================================================ */

/*
 * Test: Acquire then release restores clean state
 */
TEST(acquire_release_roundtrip) {
    uid_t uid = { 0xDEADBEEF, 0xCAFEBABE };
    uint16_t bucket = uid_lock_hash(&uid);

    reset_mocks();
    ASSERT_EQ(0, FILE_$UID_LOCK_HOLDERS[bucket]);

    FILE_$UID_LOCK_ACQUIRE(&uid);
    ASSERT_NE(0, FILE_$UID_LOCK_HOLDERS[bucket]);

    FILE_$UID_LOCK_RELEASE(&uid);
    ASSERT_EQ(0, FILE_$UID_LOCK_HOLDERS[bucket]);
    ASSERT_EQ(1, mock_ec_advance_count);
}

/*
 * Test: Multiple UIDs that hash to different buckets
 */
TEST(multiple_uids_different_buckets) {
    uid_t uids[4] = {
        { 0x00000001, 0x00000000 },  /* bucket 1 */
        { 0x00000002, 0x00000000 },  /* bucket 2 */
        { 0x00000003, 0x00000000 },  /* bucket 3 */
        { 0x00000004, 0x00000000 },  /* bucket 4 */
    };

    reset_mocks();

    for (int i = 0; i < 4; i++) {
        FILE_$UID_LOCK_ACQUIRE(&uids[i]);
    }

    /* All should be held */
    for (int i = 0; i < 4; i++) {
        uint16_t bucket = uid_lock_hash(&uids[i]);
        ASSERT_NE(0, FILE_$UID_LOCK_HOLDERS[bucket]);
    }

    /* Release all */
    for (int i = 0; i < 4; i++) {
        FILE_$UID_LOCK_RELEASE(&uids[i]);
    }

    /* All should be free */
    for (int i = 0; i < 4; i++) {
        uint16_t bucket = uid_lock_hash(&uids[i]);
        ASSERT_EQ(0, FILE_$UID_LOCK_HOLDERS[bucket]);
    }
}

/* ============================================================================
 * Tests: Hash symmetry
 * ============================================================================ */

/*
 * Test: Acquire and release must hash to the same bucket
 * (This is implicitly tested by the roundtrip test, but let's be explicit)
 */
TEST(hash_consistency) {
    uid_t uid = { 0xAAAABBBB, 0xCCCCDDDD };
    uint16_t h1 = uid_lock_hash(&uid);
    uint16_t h2 = uid_lock_hash(&uid);
    ASSERT_EQ(h1, h2);
}

/* ============================================================================
 * Main
 * ============================================================================ */

int main(void) {
    printf("=== FILE_$UID_LOCK_ACQUIRE / FILE_$UID_LOCK_RELEASE tests ===\n\n");

    printf("Hash computation tests:\n");
    RUN_TEST(hash_zero_uid);
    RUN_TEST(hash_range);
    RUN_TEST(hash_known_value_1);
    RUN_TEST(hash_known_value_2);
    RUN_TEST(hash_known_value_3);
    RUN_TEST(hash_modulo_exact);
    RUN_TEST(hash_modulo_wrap);

    printf("\nLock acquire tests:\n");
    RUN_TEST(acquire_free_slot);
    RUN_TEST(acquire_stores_pid_low_byte);
    RUN_TEST(acquire_pid_low_byte_only);

    printf("\nLock release tests:\n");
    RUN_TEST(release_clears_holder);
    RUN_TEST(release_advances_ec);
    RUN_TEST(release_correct_bucket_only);

    printf("\nRound-trip tests:\n");
    RUN_TEST(acquire_release_roundtrip);
    RUN_TEST(multiple_uids_different_buckets);

    printf("\nHash consistency tests:\n");
    RUN_TEST(hash_consistency);

    printf("\n=== Results: %d passed, %d failed ===\n",
           tests_passed, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
