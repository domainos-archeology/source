/*
 * hint/test/test_init.c - Unit tests for HINT_$INIT (0x00E3122C).
 *
 * The test compiles the real hint/init.c with every routine it calls
 * scripted, so each arm of the original can be driven: the resolve/create
 * pair, the MST_$MAPS argument block and the unconditional store of its
 * result into the module block at globals+0x20, the FILE_$LOCK argument
 * block (including the mode cell whose image bytes are 00 04), the version-1
 * early exit, the version-7 reinitialisation and the retry-once failure arm.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ==========================================================================
 * Test framework
 * ========================================================================== */

static int tests_failed = 0;
static int tests_run = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name)                                                        \
    do {                                                                      \
        printf("  Running %s... ", #name);                                    \
        tests_run++;                                                          \
        test_##name();                                                        \
        printf("done\n");                                                     \
    } while (0)

#define ASSERT_EQ(expected, actual)                                           \
    do {                                                                      \
        long long _e = (long long)(expected);                                 \
        long long _a = (long long)(actual);                                   \
        if (_e != _a) {                                                       \
            printf("FAILED\n    Expected: 0x%llx, Got: 0x%llx at line %d\n",  \
                   (unsigned long long)_e, (unsigned long long)_a, __LINE__); \
            tests_failed++;                                                   \
            return;                                                           \
        }                                                                     \
    } while (0)

/* ==========================================================================
 * Globals and mocks the code under test links against
 * ========================================================================== */

#include "hint/hint_internal.h"

hint_globals_t HINT_$GLOBALS_BLOCK;
hint_file_t *HINT_$HINTFILE_PTR;
uid_t UID_$NIL = { 0, 0 };

route_$port_t test_port0;
route_$port_t *ROUTE_$PORTP[8];
uint32_t ROUTE_$PORT;

/* The hint file the scripted MST_$MAPS hands back. */
static hint_file_t test_hintfile;

static int       resolve_calls;
static status_$t resolve_status;
static char     *resolve_path_seen;
static int16_t  *resolve_len_seen;

static int       cr_file_calls;
static status_$t cr_file_status;

static int       drop_calls;
static status_$t drop_status;

static int       delete_calls;

static int       maps_calls;
static void     *maps_result;
static status_$t maps_status;
static int16_t   maps_asid_seen;
static boolean   maps_dir_seen;
static uid_t    *maps_uid_seen;
static uint32_t  maps_start_seen;
static uint32_t  maps_len_seen;
static int16_t   maps_area_seen;
static uint32_t  maps_area_size_seen;
static boolean   maps_rights_seen;
static void     *maps_info_seen;

static int       lock_calls;
static uid_t    *lock_uid_seen;
static uint16_t  lock_index_seen;
static uint16_t  lock_mode_seen;
static uint8_t   lock_rights_seen;
static void     *lock_info_seen;
static status_$t lock_status;

static int       clear_calls;

void NAME_$RESOLVE(char *path, int16_t *path_len, uid_t *resolved_uid,
                   status_$t *status_ret)
{
    resolve_calls++;
    resolve_path_seen = path;
    resolve_len_seen = path_len;
    resolved_uid->high = 0x11112222;
    resolved_uid->low = 0x33334444;
    *status_ret = resolve_status;
}

void NAME_$CR_FILE(char *path, int16_t *path_len, uid_t *file_ret,
                   status_$t *status_ret)
{
    (void)path;
    (void)path_len;
    cr_file_calls++;
    file_ret->high = 0x55556666;
    file_ret->low = 0x77778888;
    *status_ret = cr_file_status;
}

void NAME_$DROP(char *path, int16_t *path_len, uid_t *file_uid,
                status_$t *status_ret)
{
    (void)path;
    (void)path_len;
    (void)file_uid;
    drop_calls++;
    *status_ret = drop_status;
}

void FILE_$DELETE(uid_t *file_uid, status_$t *status_ret)
{
    (void)file_uid;
    delete_calls++;
    *status_ret = status_$ok;
}

void *MST_$MAPS(int16_t asid, boolean direction, uid_t *uid, uint32_t start_va,
                uint32_t length, int16_t area_id, uint32_t area_size,
                boolean access_rights, void *map_info, status_$t *status)
{
    maps_calls++;
    maps_asid_seen = asid;
    maps_dir_seen = direction;
    maps_uid_seen = uid;
    maps_start_seen = start_va;
    maps_len_seen = length;
    maps_area_seen = area_id;
    maps_area_size_seen = area_size;
    maps_rights_seen = access_rights;
    maps_info_seen = map_info;
    *status = maps_status;
    return maps_result;
}

void FILE_$LOCK(uid_t *file_uid, const uint16_t *lock_index,
                const uint16_t *lock_mode, const uint8_t *rights,
                void *lock_info, status_$t *status_ret)
{
    lock_calls++;
    lock_uid_seen = file_uid;
    lock_index_seen = *lock_index;
    lock_mode_seen = *lock_mode;
    lock_rights_seen = *rights;
    lock_info_seen = lock_info;
    *status_ret = lock_status;
}

void HINT_$clear_hintfile(void)
{
    clear_calls++;
    test_hintfile.header.version = HINT_FILE_VERSION;
}

#include "../init.c"

/* ==========================================================================
 * Harness
 * ========================================================================== */

static void reset_state(void)
{
    /*
     * hint_globals_t.hintfile_ptr is a 32-bit target VA, so point the host
     * arena base just below the fake hint file: the VA the code stores and
     * reads back is then 0x1000, which round-trips through ARCH_PTR_TO_VA /
     * ARCH_VA_TO_PTR and is never the nil value 0.
     */
    ARCH_HOST_VA_BASE = (uintptr_t)&test_hintfile - 0x1000;

    memset(&HINT_$GLOBALS_BLOCK, 0xA5, sizeof(HINT_$GLOBALS_BLOCK));
    memset(&test_hintfile, 0, sizeof(test_hintfile));
    memset(&test_port0, 0, sizeof(test_port0));
    ROUTE_$PORTP[0] = &test_port0;
    ROUTE_$PORT = 0xDEADBEEF;
    HINT_$HINTFILE_PTR = (hint_file_t *)(uintptr_t)0x1;

    resolve_calls = 0;
    resolve_status = status_$ok;
    resolve_path_seen = NULL;
    resolve_len_seen = NULL;
    cr_file_calls = 0;
    cr_file_status = status_$ok;
    drop_calls = 0;
    drop_status = status_$ok;
    delete_calls = 0;

    maps_calls = 0;
    maps_result = &test_hintfile;
    maps_status = status_$ok;

    lock_calls = 0;
    lock_status = status_$ok;
    lock_index_seen = 0xFFFF;
    lock_mode_seen = 0xFFFF;
    lock_rights_seen = 0xFF;
    lock_info_seen = NULL;

    clear_calls = 0;

    /* A file that is already fully initialised unless a test says otherwise. */
    test_hintfile.header.version = HINT_FILE_VERSION;
}

/* ==========================================================================
 * Constant cells
 * ========================================================================== */

/*
 * The four constant cells, byte for byte from the image
 * (0xE313AA..0xE313C7):
 *   00 14 | 00 00 | 00 04 | 00 00 | "`node_data/hint_file"
 */
TEST(constant_cells_match_the_image)
{
    ASSERT_EQ(20, hint_file_path_length);
    ASSERT_EQ(20, HINT_FILE_PATH_LEN);
    ASSERT_EQ(20, sizeof(hint_file_path));
    ASSERT_EQ(0, memcmp(hint_file_path, "`node_data/hint_file", 20));

    ASSERT_EQ(0x0000, hint_lock_index);
    /* 0xE313AE holds 00 04 - the tree used to pass 0 here. */
    ASSERT_EQ(0x0004, hint_lock_mode);
    ASSERT_EQ(0x00, hint_lock_rights[0]);
}

/* ==========================================================================
 * The happy path
 * ========================================================================== */

/*
 * 0x00E31250-0x00E3126E and 0x00E31292-0x00E312B8: the resolve gets the
 * pathname and length cells, and the map gets the ten arguments the push
 * sequence spells out.
 */
TEST(resolve_and_map_argument_block)
{
    reset_state();
    HINT_$INIT();

    ASSERT_EQ(1, resolve_calls);
    ASSERT_EQ((uintptr_t)hint_file_path, (uintptr_t)resolve_path_seen);
    ASSERT_EQ((uintptr_t)&hint_file_path_length, (uintptr_t)resolve_len_seen);
    ASSERT_EQ(0, cr_file_calls);

    ASSERT_EQ(1, maps_calls);
    ASSERT_EQ(0, maps_asid_seen);
    ASSERT_EQ((int8_t)0xFF, maps_dir_seen);
    ASSERT_EQ(0, maps_start_seen);
    ASSERT_EQ(0x7FFF, maps_len_seen);
    ASSERT_EQ(0x16, maps_area_seen);
    ASSERT_EQ(0, maps_area_size_seen);
    ASSERT_EQ((int8_t)0xFF, maps_rights_seen);
}

/*
 * 0x00E312BA-0x00E312C2: the map result lands in the module block at
 * globals+0x20 unconditionally, before the status test - not in a local.
 */
TEST(map_result_goes_into_the_module_block)
{
    reset_state();
    HINT_$INIT();

    ASSERT_EQ(ARCH_PTR_TO_VA(&test_hintfile), HINT_$GLOBALS->hintfile_ptr);
    ASSERT_EQ((uintptr_t)&test_hintfile, (uintptr_t)HINT_$HINTFILE_PTR);

    /* Even when the map reports an error the cell has been written. */
    reset_state();
    maps_status = 0x000B0002;
    HINT_$INIT();
    ASSERT_EQ(ARCH_PTR_TO_VA(&test_hintfile), HINT_$GLOBALS->hintfile_ptr);
    ASSERT_EQ(0, lock_calls);
}

/*
 * 0x00E312CC-0x00E312EC: FILE_$LOCK's six arguments.  The mode cell is 4, the
 * index and rights cells are 0, and the fifth argument is a real frame slot.
 */
TEST(file_lock_argument_block)
{
    reset_state();
    HINT_$INIT();

    ASSERT_EQ(1, lock_calls);
    ASSERT_EQ(0x0000, lock_index_seen);
    ASSERT_EQ(0x0004, lock_mode_seen);
    ASSERT_EQ(0x00, lock_rights_seen);
    ASSERT_EQ(1, lock_info_seen != NULL);
}

/*
 * 0x00E312EE-0x00E31300: the version is read through globals+0x20, and a
 * version-1 file returns at once, leaving HINT_$HINTFILE_PTR untouched by the
 * publish at 0x00E31302.
 */
TEST(version_one_returns_before_publishing)
{
    reset_state();
    HINT_$HINTFILE_PTR = NULL;      /* the entry store at 0x00E31236 */
    test_hintfile.header.version = HINT_FILE_UNINIT;

    HINT_$INIT();

    ASSERT_EQ(1, maps_calls);
    ASSERT_EQ(1, lock_calls);
    /* The module block still points at the mapped file ... */
    ASSERT_EQ(ARCH_PTR_TO_VA(&test_hintfile), HINT_$GLOBALS->hintfile_ptr);
    /* ... but the published pointer was never set. */
    ASSERT_EQ(0, (uintptr_t)HINT_$HINTFILE_PTR);
    ASSERT_EQ(0, clear_calls);
    /* And ROUTE_$PORT was not touched. */
    ASSERT_EQ(0xDEADBEEF, ROUTE_$PORT);
}

/* 0x00E31316-0x00E31324: any version other than 7 is reinitialised. */
TEST(non_seven_version_is_cleared)
{
    reset_state();
    test_hintfile.header.version = 3;
    HINT_$INIT();
    ASSERT_EQ(1, clear_calls);

    reset_state();
    test_hintfile.header.version = HINT_FILE_VERSION;
    HINT_$INIT();
    ASSERT_EQ(0, clear_calls);
}

/*
 * 0x00E31326-0x00E3135E: both halves of net_info must match port 0's
 * port_type and socket for the stored network port to be adopted.
 */
TEST(network_match_controls_route_port)
{
    reset_state();
    test_hintfile.header.net_info = 0x00020007;
    test_hintfile.header.net_port = 0x0A0B0C0D;
    test_port0.port_type = 0x0002;
    test_port0.socket = 0x0007;
    HINT_$INIT();
    ASSERT_EQ(0x0A0B0C0D, ROUTE_$PORT);

    /* High half differs. */
    reset_state();
    test_hintfile.header.net_info = 0x00030007;
    test_hintfile.header.net_port = 0x0A0B0C0D;
    test_port0.port_type = 0x0002;
    test_port0.socket = 0x0007;
    HINT_$INIT();
    ASSERT_EQ(0, ROUTE_$PORT);

    /* Low half differs. */
    reset_state();
    test_hintfile.header.net_info = 0x00020008;
    test_hintfile.header.net_port = 0x0A0B0C0D;
    test_port0.port_type = 0x0002;
    test_port0.socket = 0x0007;
    HINT_$INIT();
    ASSERT_EQ(0, ROUTE_$PORT);
}

/* ==========================================================================
 * The failure arm
 * ========================================================================== */

/*
 * 0x00E31270-0x00E31290: a failing resolve tries a create, and a failing
 * create enters the failure arm.
 */
TEST(resolve_failure_creates_then_retries_once)
{
    reset_state();
    resolve_status = 0x000E0001;
    cr_file_status = status_$ok;

    HINT_$INIT();

    ASSERT_EQ(1, resolve_calls);
    ASSERT_EQ(1, cr_file_calls);
    ASSERT_EQ(1, maps_calls);
    ASSERT_EQ(0, drop_calls);
}

/*
 * 0x00E31360-0x00E31398: the first failure drops and, when the drop
 * succeeds, deletes the file, then loops back to 0x00E31250.  The second
 * failure takes 0x00E3139A and leaves the published pointer null.
 */
TEST(second_failure_gives_up_with_a_null_pointer)
{
    reset_state();
    resolve_status = 0x000E0001;
    cr_file_status = 0x000E0002;

    HINT_$INIT();

    /* Two passes through the loop. */
    ASSERT_EQ(2, resolve_calls);
    ASSERT_EQ(2, cr_file_calls);
    /* Only the first failure runs the cleanup. */
    ASSERT_EQ(1, drop_calls);
    ASSERT_EQ(1, delete_calls);
    ASSERT_EQ(0, maps_calls);
    ASSERT_EQ(0, (uintptr_t)HINT_$HINTFILE_PTR);
}

/* 0x00E31382 "bne.b 0x00E31394": a failing drop skips the delete. */
TEST(failed_drop_skips_the_delete)
{
    reset_state();
    resolve_status = 0x000E0001;
    cr_file_status = 0x000E0002;
    drop_status = 0x000E0003;

    HINT_$INIT();

    ASSERT_EQ(1, drop_calls);
    ASSERT_EQ(0, delete_calls);
    ASSERT_EQ(2, resolve_calls);
}

/* A map failure takes the same failure arm as a create failure. */
TEST(map_failure_retries_then_gives_up)
{
    reset_state();
    maps_status = 0x000B0002;

    HINT_$INIT();

    ASSERT_EQ(2, resolve_calls);
    ASSERT_EQ(2, maps_calls);
    ASSERT_EQ(1, drop_calls);
    ASSERT_EQ(0, lock_calls);
    ASSERT_EQ(0, (uintptr_t)HINT_$HINTFILE_PTR);
}

int main(void)
{
    printf("HINT_$INIT tests\n");

    RUN_TEST(constant_cells_match_the_image);
    RUN_TEST(resolve_and_map_argument_block);
    RUN_TEST(map_result_goes_into_the_module_block);
    RUN_TEST(file_lock_argument_block);
    RUN_TEST(version_one_returns_before_publishing);
    RUN_TEST(non_seven_version_is_cleared);
    RUN_TEST(network_match_controls_route_port);
    RUN_TEST(resolve_failure_creates_then_retries_once);
    RUN_TEST(second_failure_gives_up_with_a_null_pointer);
    RUN_TEST(failed_drop_skips_the_delete);
    RUN_TEST(map_failure_retries_then_gives_up);

    printf("%d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed != 0;
}
