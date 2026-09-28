/*
 * flop/test/test_boot.c - FLOP_$BOOT (0x00E3254C) with its nested
 * flop_$boot_errchk (0x00E323A8) and the constant cells it shares with
 * flop_$mount_floppy (0x00E323E6).
 *
 * The regression this file exists for (source-y89n): MST_$MAP_AT's seventh
 * argument is `pea (-0x116,PC)` at 0x00E3264C, i.e. the byte at
 * 0x00E3264E - 0x116 = 0x00E32538, whose image byte is 0x00 - the SAME cell
 * FILE_$LOCK gets as `rights`.  It is NOT 0x00E32542 (0xFF), which is what
 * MST_$MAP receives at 0x00E325CE.
 *
 * Both flop .c files are included, so the mocks sit below them: VOLX / DIR /
 * NAME for the mount step, FILE / MST for the boot shell, and OS_$BOOT_ERRCHK
 * to observe what the nested error check hands on.
 */

#include <stdio.h>
#include <string.h>

#include "flop/flop_internal.h"

/* ==========================================================================
 * Test framework
 * ========================================================================== */

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  Running %-44s ", #name); \
    test_##name(); \
    tests_passed++; \
    printf("PASSED\n"); \
} while (0)

#define ASSERT_EQ(expected, actual) do { \
    unsigned long long _e = (unsigned long long)(expected); \
    unsigned long long _a = (unsigned long long)(actual); \
    if (_e != _a) { \
        printf("FAILED\n    Expected: 0x%llx, Got: 0x%llx at line %d\n", \
               _e, _a, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

#define ASSERT_PTR_EQ(expected, actual) do { \
    const void *_e = (const void *)(expected); \
    const void *_a = (const void *)(actual); \
    if (_e != _a) { \
        printf("FAILED\n    Expected: %p, Got: %p at line %d\n", \
               _e, _a, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

#define ASSERT_STR_EQ(expected, actual) do { \
    if (strcmp((expected), (actual)) != 0) { \
        printf("FAILED\n    Expected: \"%s\", Got: \"%s\" at line %d\n", \
               (expected), (actual), __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

/* ==========================================================================
 * Globals the code under test references
 * ========================================================================== */

uid_t UID_$NIL = { 0, 0 };

/* ==========================================================================
 * Mock bookkeeping
 * ========================================================================== */

static status_$t mock_status_after_mount;       /* VOLX_$MOUNT */
static status_$t mock_status_after_resolve;     /* NAME_$RESOLVE */
static status_$t mock_status_after_lock;
static status_$t mock_status_after_map;
static status_$t mock_status_after_unmap;
static status_$t mock_status_after_map_at;

static int         mock_mount_calls;
static int         mock_dismount_calls;
static int         mock_addu_calls;
static int         mock_set_dad_calls;

static int         mock_errchk_calls;
static const char *mock_errchk_msg[8];
static const char *mock_errchk_arg[8];
static int16_t     mock_errchk_arg_len[8];
static status_$t  *mock_errchk_status_ptr[8];

static int              mock_resolve_calls;
static const char      *mock_resolve_path;
static const int16_t   *mock_resolve_len_ptr;
static int16_t          mock_resolve_len;

static int          mock_lock_calls;
static const void  *mock_lock_index_ptr;
static const void  *mock_lock_mode_ptr;
static const void  *mock_lock_rights_ptr;

static int    mock_map_calls;
static void  *mock_map_start_ptr;
static void  *mock_map_length_ptr;
static void  *mock_map_mode_ptr;
static void  *mock_map_extend_ptr;
static void  *mock_map_concur_ptr;

static int    mock_map_at_calls;
static void  *mock_map_at_va;
static void  *mock_map_at_start_ptr;
static void  *mock_map_at_length_ptr;
static void  *mock_map_at_mode_ptr;
static void  *mock_map_at_extend_ptr;
static void  *mock_map_at_concur_ptr;

static int       mock_unmap_calls;
static uint32_t  mock_unmap_va;

static uint32_t mock_file_image[8];

/* ==========================================================================
 * Mocks
 * ========================================================================== */

void VOLX_$MOUNT(int16_t *dev, int16_t *bus, int16_t *ctlr, int16_t *lv_num,
                 int8_t *salvage_ok, int8_t *write_prot, uid_t *parent_uid,
                 uid_t *dir_uid_ret, status_$t *status)
{
    (void)dev; (void)bus; (void)ctlr; (void)lv_num; (void)salvage_ok;
    (void)write_prot; (void)parent_uid;
    mock_mount_calls++;
    dir_uid_ret->high = 0xF10BF10Bu;
    dir_uid_ret->low  = 0x00000001u;
    *status = mock_status_after_mount;
}

void VOLX_$DISMOUNT(int16_t *dev, int16_t *bus, int16_t *ctlr, int16_t *lv_num,
                    uid_t *entry_uid, int8_t *force, status_$t *status)
{
    (void)dev; (void)bus; (void)ctlr; (void)lv_num; (void)entry_uid; (void)force;
    mock_dismount_calls++;
    *status = status_$ok;
}

void NAME_$GET_NODE_UID(uid_t *node_uid)
{
    node_uid->high = 0x0000A0DEu;
    node_uid->low  = 0x00000002u;
}

void DIR_$ADDU(uid_t *dir_uid, char *name, int16_t *name_len, uid_t *entry_uid,
               status_$t *status_ret)
{
    (void)dir_uid; (void)name; (void)name_len; (void)entry_uid;
    mock_addu_calls++;
    *status_ret = status_$ok;
}

void DIR_$SET_DAD(uid_t *dir_uid, uid_t *parent_uid, status_$t *status_ret)
{
    (void)dir_uid; (void)parent_uid;
    mock_set_dad_calls++;
    *status_ret = status_$ok;
}

void DIR_$DROPU(uid_t *dir_uid, char *name, uint16_t *name_len,
                uid_t *entry_uid, status_$t *status_ret)
{
    (void)dir_uid; (void)name; (void)name_len; (void)entry_uid;
    *status_ret = status_$ok;
}

char OS_$BOOT_ERRCHK(const char *format_str, const char *arg_str,
                     short *line_ptr, status_$t *status)
{
    if (mock_errchk_calls < 8) {
        mock_errchk_msg[mock_errchk_calls] = format_str;
        mock_errchk_arg[mock_errchk_calls] = arg_str;
        mock_errchk_arg_len[mock_errchk_calls] = *line_ptr;
        mock_errchk_status_ptr[mock_errchk_calls] = status;
    }
    mock_errchk_calls++;
    return (*status == status_$ok) ? (char)0xFF : 0;
}

void NAME_$RESOLVE(char *path, int16_t *path_len, uid_t *resolved_uid,
                   status_$t *status_ret)
{
    mock_resolve_calls++;
    mock_resolve_path    = path;
    mock_resolve_len_ptr = path_len;
    mock_resolve_len     = *path_len;
    resolved_uid->high = 0x11223344u;
    resolved_uid->low  = 0x55667788u;
    *status_ret = mock_status_after_resolve;
}

void FILE_$LOCK(uid_t *file_uid, const uint16_t *lock_index,
                const uint16_t *lock_mode, const uint8_t *rights,
                void *lock_info, status_$t *status_ret)
{
    (void)file_uid; (void)lock_info;
    mock_lock_calls++;
    mock_lock_index_ptr  = lock_index;
    mock_lock_mode_ptr   = lock_mode;
    mock_lock_rights_ptr = rights;
    *status_ret = mock_status_after_lock;
}

void *MST_$MAP(uid_t *uid, uint32_t *start_ptr, uint32_t *length_ptr,
               uint16_t *mode_ptr, uint32_t *extend_ptr,
               uint8_t *concur_ptr, void *map_info, status_$t *status_ret)
{
    (void)uid; (void)map_info;
    mock_map_calls++;
    mock_map_start_ptr  = start_ptr;
    mock_map_length_ptr = length_ptr;
    mock_map_mode_ptr   = mode_ptr;
    mock_map_extend_ptr = extend_ptr;
    mock_map_concur_ptr = concur_ptr;
    *status_ret = mock_status_after_map;
    return mock_file_image;
}

void MST_$UNMAP(uid_t *uid, uint32_t *start_ptr, uint32_t *map_info,
                status_$t *status_ret)
{
    (void)uid; (void)map_info;
    mock_unmap_calls++;
    mock_unmap_va = *start_ptr;
    *status_ret = mock_status_after_unmap;
}

void MST_$MAP_AT(void *start, uid_t *uid, void *param1, void *param2,
                 void *param3, void *param4, void *param5, void *result,
                 status_$t *status)
{
    (void)uid; (void)result;
    mock_map_at_calls++;
    mock_map_at_va         = start;
    mock_map_at_start_ptr  = param1;
    mock_map_at_length_ptr = param2;
    mock_map_at_mode_ptr   = param3;
    mock_map_at_extend_ptr = param4;
    mock_map_at_concur_ptr = param5;
    *status = mock_status_after_map_at;
}

/* ==========================================================================
 * Code under test
 * ========================================================================== */

#include "../mount.c"
#include "../boot.c"

static void reset(void)
{
    int i;

    mock_status_after_mount   = status_$ok;
    mock_status_after_resolve = status_$ok;
    mock_status_after_lock    = status_$ok;
    mock_status_after_map     = status_$ok;
    mock_status_after_unmap   = status_$ok;
    mock_status_after_map_at  = status_$ok;

    mock_mount_calls = 0;
    mock_dismount_calls = 0;
    mock_addu_calls = 0;
    mock_set_dad_calls = 0;
    mock_errchk_calls = 0;
    memset(mock_errchk_msg, 0, sizeof(mock_errchk_msg));
    memset(mock_errchk_arg, 0, sizeof(mock_errchk_arg));
    memset(mock_errchk_arg_len, 0, sizeof(mock_errchk_arg_len));
    memset(mock_errchk_status_ptr, 0, sizeof(mock_errchk_status_ptr));
    mock_resolve_calls = 0;
    mock_lock_calls = 0;
    mock_map_calls = 0;
    mock_map_at_calls = 0;
    mock_unmap_calls = 0;

    for (i = 0; i < 8; i++) {
        mock_file_image[i] = 0xC0DE0000u + (uint32_t)i;
    }

    /* the constant cells must survive a run unchanged */
    flop_zero_byte = 0x00;
    flop_word_four = 0x0004;
    flop_word_one = 0x0001;
    flop_ff_byte = 0xFF;
    flop_map_mode = 0x0007;
    flop_map_length = 0x00100000;
    flop_map_start = 0;
    flop_boot_shell_path_len = 0x0013;
    flop_trying_normal_shell_len = 0x0015;
}

/* ==========================================================================
 * Tests
 * ========================================================================== */

/* The image bytes at each `pea (d,PC)` cell. */
TEST(constant_cell_values)
{
    reset();
    ASSERT_EQ(0x00, flop_zero_byte);            /* 0x00E32538 */
    ASSERT_EQ(0x0004, flop_word_four);          /* 0x00E3253A */
    ASSERT_EQ(0x0001, flop_word_one);           /* 0x00E32540 */
    ASSERT_EQ(0xFF, flop_ff_byte);              /* 0x00E32542 */
    ASSERT_EQ(0x0007, flop_map_mode);           /* 0x00E326C6 */
    ASSERT_EQ(0x00100000, flop_map_length);     /* 0x00E3272C */
    ASSERT_EQ(0, flop_map_start);               /* 0x00E32730 */
    ASSERT_EQ(0x0013, flop_boot_shell_path_len);/* 0x00E326DE */
    ASSERT_EQ(19, strlen(flop_boot_shell_path));/* 0x00E326E0 */
    ASSERT_STR_EQ("/flp/sys/boot_shell", flop_boot_shell_path);
}

/* The five console messages, each '%'-terminated as in the image. */
TEST(message_strings)
{
    ASSERT_STR_EQ("bad floppy mount%", flop_bad_floppy_mount_msg);
    ASSERT_STR_EQ("can't lock boot shell%", flop_cant_lock_msg);
    ASSERT_STR_EQ("can't map boot shell%", flop_cant_map_msg);
    ASSERT_STR_EQ("can't unmap boot shell%", flop_cant_unmap_msg);
    ASSERT_STR_EQ("can't map at indicated address%", flop_cant_map_at_msg);
}

/*
 * 0x00E323CE/0x00E323D0: the fallback text is the 21-character
 * "- trying normal shell" with its length word 0x0015, and the status the
 * nested procedure passes on is FLOP_$BOOT's own status_ret (0x00E323B0
 * reads it through the static link).
 */
TEST(errchk_passes_fallback_text_and_uplevel_status)
{
    uint32_t entry = 0;
    status_$t status = 0;

    reset();
    (void)FLOP_$BOOT(&entry, &status);

    ASSERT_EQ(5, mock_errchk_calls);
    ASSERT_STR_EQ("- trying normal shell", mock_errchk_arg[0]);
    ASSERT_EQ(21, strlen(mock_errchk_arg[0]));
    ASSERT_EQ(0x0015, mock_errchk_arg_len[0]);
    ASSERT_PTR_EQ(flop_trying_normal_shell, mock_errchk_arg[4]);
    ASSERT_PTR_EQ(&status, mock_errchk_status_ptr[0]);
    ASSERT_PTR_EQ(&status, mock_errchk_status_ptr[4]);
}

/*
 * The regression: MST_$MAP_AT's concurrency argument is the 0x00 cell, and
 * it is the very same object FILE_$LOCK got as `rights`.
 */
TEST(map_at_concurrency_is_the_zero_byte_cell)
{
    uint32_t entry = 0;
    status_$t status = 0;

    reset();
    (void)FLOP_$BOOT(&entry, &status);

    ASSERT_EQ(1, mock_map_at_calls);
    ASSERT_PTR_EQ(&flop_zero_byte, mock_map_at_concur_ptr);
    ASSERT_EQ(0x00, *(const uint8_t *)mock_map_at_concur_ptr);

    /* the same cell FILE_$LOCK received */
    ASSERT_PTR_EQ(mock_lock_rights_ptr, mock_map_at_concur_ptr);

    /* and NOT the 0xFF cell MST_$MAP got */
    ASSERT_PTR_EQ(&flop_ff_byte, mock_map_concur_ptr);
    ASSERT_EQ(0xFF, *(const uint8_t *)mock_map_concur_ptr);
}

/* 0x00E325D2/0x00E325DE and 0x00E32650/0x00E3265C: one cell, passed twice. */
TEST(start_and_extend_are_one_cell)
{
    uint32_t entry = 0;
    status_$t status = 0;

    reset();
    (void)FLOP_$BOOT(&entry, &status);

    ASSERT_PTR_EQ(&flop_map_start, mock_map_start_ptr);
    ASSERT_PTR_EQ(&flop_map_start, mock_map_extend_ptr);
    ASSERT_PTR_EQ(&flop_map_start, mock_map_at_start_ptr);
    ASSERT_PTR_EQ(&flop_map_start, mock_map_at_extend_ptr);

    ASSERT_PTR_EQ(&flop_map_length, mock_map_length_ptr);
    ASSERT_PTR_EQ(&flop_map_length, mock_map_at_length_ptr);
    ASSERT_PTR_EQ(&flop_map_mode, mock_map_mode_ptr);
    ASSERT_PTR_EQ(&flop_map_mode, mock_map_at_mode_ptr);
}

/* FILE_$LOCK's three by-reference constants (0x00E3259A-0x00E325A2) are the
 * cells flop_$mount_floppy uses as dev/lv, the "/flp" length and write_prot. */
TEST(file_lock_constants)
{
    uint32_t entry = 0;
    status_$t status = 0;

    reset();
    (void)FLOP_$BOOT(&entry, &status);

    ASSERT_EQ(1, mock_lock_calls);
    ASSERT_PTR_EQ(&flop_word_one, mock_lock_index_ptr);
    ASSERT_PTR_EQ(&flop_word_four, mock_lock_mode_ptr);
    ASSERT_PTR_EQ(&flop_zero_byte, mock_lock_rights_ptr);
}

/* NAME_$RESOLVE gets the path AND the length cell by address.  (The mount
 * step does not resolve anything when DIR_$ADDU succeeds.) */
TEST(name_resolve_arguments)
{
    uint32_t entry = 0;
    status_$t status = 0;

    reset();
    (void)FLOP_$BOOT(&entry, &status);

    ASSERT_EQ(1, mock_resolve_calls);
    ASSERT_PTR_EQ(flop_boot_shell_path, mock_resolve_path);
    ASSERT_PTR_EQ(&flop_boot_shell_path_len, mock_resolve_len_ptr);
    ASSERT_EQ(19, mock_resolve_len);
}

/* 0x00E32606-0x00E32616 / 0x00E32684: header[1] is the entry point. */
TEST(entry_point_is_header_word_1)
{
    uint32_t entry = 0;
    status_$t status = 0;
    int8_t ok;

    reset();
    ok = FLOP_$BOOT(&entry, &status);

    ASSERT_EQ(-1, ok);
    ASSERT_EQ(0xC0DE0001u, entry);
    /* the pointer MST_$MAP returned is what MST_$UNMAP is given back */
    ASSERT_EQ(ARCH_PTR_TO_VA(mock_file_image), mock_unmap_va);
}

/*
 * 0x00E32568-0x00E3256E: the error check runs before the status test, so a
 * mount failure still prints - and the routine falls to the common tail
 * rather than returning early.
 */
TEST(mount_failure_stops_after_the_first_errchk)
{
    uint32_t entry = 0xEEEE;
    status_$t status = 0;
    int8_t ok;

    reset();
    mock_status_after_mount = 0x000B0004;
    ok = FLOP_$BOOT(&entry, &status);

    ASSERT_EQ(0, ok);
    ASSERT_EQ(0x000B0004, status);
    ASSERT_EQ(1, mock_mount_calls);
    ASSERT_EQ(0, mock_addu_calls);
    ASSERT_EQ(1, mock_errchk_calls);
    ASSERT_PTR_EQ(flop_bad_floppy_mount_msg, mock_errchk_msg[0]);
    ASSERT_EQ(0, mock_resolve_calls);
    ASSERT_EQ(0xEEEE, entry);       /* the output is left alone */
}

/* 0x00E3258A: NAME_$RESOLVE has no errchk of its own in the image. */
TEST(resolve_failure_has_no_message)
{
    uint32_t entry = 0;
    status_$t status = 0;

    reset();
    mock_status_after_resolve = 0x000E0002;
    (void)FLOP_$BOOT(&entry, &status);

    ASSERT_EQ(1, mock_errchk_calls);            /* only the mount message */
    ASSERT_EQ(0, mock_lock_calls);
}

/* Every step's message, in the order the image emits them. */
TEST(message_order_on_the_success_path)
{
    uint32_t entry = 0;
    status_$t status = 0;

    reset();
    (void)FLOP_$BOOT(&entry, &status);

    ASSERT_EQ(5, mock_errchk_calls);
    ASSERT_PTR_EQ(flop_bad_floppy_mount_msg, mock_errchk_msg[0]);  /* 0x00E3255E */
    ASSERT_PTR_EQ(flop_cant_lock_msg, mock_errchk_msg[1]);         /* 0x00E325B4 */
    ASSERT_PTR_EQ(flop_cant_map_msg, mock_errchk_msg[2]);          /* 0x00E325F2 */
    ASSERT_PTR_EQ(flop_cant_unmap_msg, mock_errchk_msg[3]);        /* 0x00E32634 */
    ASSERT_PTR_EQ(flop_cant_map_at_msg, mock_errchk_msg[4]);       /* 0x00E32672 */
}

int main(void)
{
    printf("FLOP_$BOOT tests\n");
    RUN_TEST(constant_cell_values);
    RUN_TEST(message_strings);
    RUN_TEST(errchk_passes_fallback_text_and_uplevel_status);
    RUN_TEST(map_at_concurrency_is_the_zero_byte_cell);
    RUN_TEST(start_and_extend_are_one_cell);
    RUN_TEST(file_lock_constants);
    RUN_TEST(name_resolve_arguments);
    RUN_TEST(entry_point_is_header_word_1);
    RUN_TEST(mount_failure_stops_after_the_first_errchk);
    RUN_TEST(resolve_failure_has_no_message);
    RUN_TEST(message_order_on_the_success_path);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
