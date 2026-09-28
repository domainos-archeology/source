/*
 * name/test/test_init.c - unit tests for NAME_$INIT (0x00E31624) and its
 * nested name_$init_check_status (0x00E31578).
 *
 * The real name/init.c is #included (it defines NAME_$DATA and
 * NAME_$CANNED_ROOT_UID itself).  Everything it calls is mocked:
 * ACL_$ENTER_SUPER/EXIT_SUPER, DIR_$INIT, VTOC_$GET_NAME_DIRS, name_$map_dir,
 * NAME_$RESOLVE, FILE_$SET_DIRPTR, VFMT_$FORMATN, VFMT_$WRITE10, TIME_$WAIT
 * and CRASH_SYSTEM.
 */

#include <stdio.h>
#include <string.h>
#include <stdarg.h>

static int tests_passed = 0;
static int tests_failed = 0;
static int current_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do {                     \
    printf("  %-52s ", #name);                  \
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

#include "name/name_internal.h"
#include "node/node.h"

uid_t    UID_$NIL = { 0, 0 };
int16_t  CAL_$BOOT_VOLX;
uint32_t NODE_$ME;

/* ------------------------------------------------------------------ */
/* Mocks                                                                */
/* ------------------------------------------------------------------ */

static int enter_super_calls, exit_super_calls, dir_init_calls;
void ACL_$ENTER_SUPER(void) { enter_super_calls++; }
void ACL_$EXIT_SUPER(void)  { exit_super_calls++; }
void DIR_$INIT(void)        { dir_init_calls++; }

static const uid_t VTOC_ROOT = { 0x0A0A0A0A, 1 };
static const uid_t VTOC_NODE = { 0x0B0B0B0B, 2 };
static int       vtoc_calls;
static int16_t   vtoc_volx_seen;
static status_$t vtoc_status;

void VTOC_$GET_NAME_DIRS(int16_t vol_idx, uid_t *dir1_uid, uid_t *dir2_uid,
                         status_$t *status_ret)
{
    vtoc_calls++;
    vtoc_volx_seen = vol_idx;
    *dir1_uid = VTOC_ROOT;
    *dir2_uid = VTOC_NODE;
    *status_ret = vtoc_status;
}

/* name_$map_dir: one scripted result per call */
static int                  map_calls;
static status_$t            map_status[4];
static boolean              map_result[4];
static uid_t                map_uid_seen[4];
static int16_t              map_asid_seen[4];
static name_$mapped_info_t *map_info_seen[4];

boolean name_$map_dir(uid_t *dir_uid, int16_t asid,
                      name_$mapped_info_t *mapped_info, status_$t *status_ret)
{
    int n = map_calls++;
    map_uid_seen[n] = *dir_uid;
    map_asid_seen[n] = asid;
    map_info_seen[n] = mapped_info;
    if (map_result[n] < 0) {
        mapped_info->active = -1;
        mapped_info->first_base = 0x1000u * (uint32_t)(n + 1);
    }
    *status_ret = map_status[n];
    return map_result[n];
}

/* NAME_$RESOLVE: records the path it saw, answers per call */
static int       resolve_calls;
static char      resolve_path_seen[4][64];
static int16_t   resolve_len_seen[4];
static uid_t    *resolve_out_seen[4];
static status_$t resolve_status[4];
static uid_t     resolve_uid[4];

void NAME_$RESOLVE(char *path, int16_t *path_len, uid_t *resolved_uid, status_$t *status_ret)
{
    int n = resolve_calls++;
    int len = *path_len > 63 ? 63 : *path_len;
    memcpy(resolve_path_seen[n], path, len);
    resolve_path_seen[n][len] = 0;
    resolve_len_seen[n] = *path_len;
    resolve_out_seen[n] = resolved_uid;
    *resolved_uid = resolve_uid[n];
    *status_ret = resolve_status[n];
}

static int       dirptr_calls;
static uid_t     dirptr_file_seen, dirptr_dir_seen;
static status_$t dirptr_status;

void FILE_$SET_DIRPTR(uid_t *file_uid, uid_t *dir_uid, status_$t *status_ret)
{
    dirptr_calls++;
    dirptr_file_seen = *file_uid;
    dirptr_dir_seen = *dir_uid;
    *status_ret = dirptr_status;
}

/* VFMT_$FORMATN: writes "/sys/node_data." + 8 upper-case hex digits of the
 * first extra argument, records all six arguments. */
static int             formatn_calls;
static const char     *formatn_fmt_seen;
static char           *formatn_buf_seen;
static int16_t         formatn_maxlen_seen;
static const uint32_t *formatn_arg5_seen;
static const uint32_t *formatn_arg6_seen;

void VFMT_$FORMATN(const char *format, char *buf, int16_t *max_len,
                   int16_t *out_len, ...)
{
    va_list ap;
    const uint32_t *node;
    static const char hex[] = "0123456789ABCDEF";
    int i;

    formatn_calls++;
    formatn_fmt_seen = format;
    formatn_buf_seen = buf;
    formatn_maxlen_seen = *max_len;
    va_start(ap, out_len);
    node = va_arg(ap, const uint32_t *);
    formatn_arg5_seen = node;
    formatn_arg6_seen = va_arg(ap, const uint32_t *);
    va_end(ap);

    memcpy(buf, "/sys/node_data.", 15);
    for (i = 0; i < 8; i++) {
        buf[15 + i] = hex[(*node >> (28 - 4 * i)) & 0xF];
    }
    *out_len = 23;
}

static int         write10_calls;
static const char *write10_fmt_seen[8];
static const void *write10_arg1_seen[8];

void VFMT_$WRITE10(const char *format, ...)
{
    va_list ap;
    int n = write10_calls++;
    write10_fmt_seen[n] = format;
    va_start(ap, format);
    write10_arg1_seen[n] = va_arg(ap, const void *);
    va_end(ap);
}

static int       wait_calls;
static uint16_t  wait_type_seen;
static uint32_t  wait_high_seen;
static uint16_t  wait_low_seen;

void TIME_$WAIT(uint16_t *delay_type, clock_t *delay, status_$t *status)
{
    wait_calls++;
    wait_type_seen = *delay_type;
    wait_high_seen = delay->high;
    wait_low_seen = delay->low;
    *status = status_$ok;
}

static int              crash_calls;
static status_$t        crash_status_seen;

void CRASH_SYSTEM(const status_$t *status_p)
{
    crash_calls++;
    crash_status_seen = *status_p;
}

#include "../init.c"

/* ------------------------------------------------------------------ */

static const uid_t ARG_ROOT = { 0x01010101, 0x11 };
static const uid_t ARG_NODE = { 0x02020202, 0x22 };
static const uid_t COM_UID  = { 0x03030303, 0x33 };
static const uid_t ND_UID   = { 0x04040404, 0x44 };

static void reset(void)
{
    memset(&NAME_$DATA, 0xEE, sizeof(NAME_$DATA));
    memset(name_$init_path_buf, 0, sizeof(name_$init_path_buf));
    CAL_$BOOT_VOLX = 3;
    NODE_$ME = 0x0000ABCD;
    enter_super_calls = exit_super_calls = dir_init_calls = 0;
    vtoc_calls = 0; vtoc_status = status_$ok;
    map_calls = 0;
    memset(map_status, 0, sizeof(map_status));
    map_result[0] = map_result[1] = map_result[2] = map_result[3] = (boolean)-1;
    resolve_calls = 0;
    memset(resolve_status, 0, sizeof(resolve_status));
    resolve_uid[0] = COM_UID;
    resolve_uid[1] = ND_UID;
    dirptr_calls = 0; dirptr_status = status_$ok;
    formatn_calls = 0;
    write10_calls = 0;
    wait_calls = 0;
    crash_calls = 0;
}

static int uid_eq(const uid_t *a, const uid_t *b)
{
    return a->high == b->high && a->low == b->low;
}

/* Supplied UIDs: no VTOC, no SET_DIRPTR, node id lower-cased, len 23. */
TEST(supplied_uids_path)
{
    uid_t root = ARG_ROOT, node = ARG_NODE;
    int i;

    reset();
    NAME_$INIT(&root, &node);

    ASSERT_EQ(1, enter_super_calls);
    ASSERT_EQ(1, dir_init_calls);
    ASSERT_EQ(0, vtoc_calls);
    ASSERT_EQ(1, uid_eq(&NAME_$DATA.root_uid, &ARG_ROOT));
    ASSERT_EQ(1, uid_eq(&NAME_$DATA.node_uid, &ARG_NODE));

    /* 58 slots initialised (0x00E316BE loop) */
    for (i = 0; i < NAME_$MAX_ASIDS; i++) {
        ASSERT_EQ(1, uid_eq(&NAME_$DATA.wdir_uid[i], &ARG_NODE));
        ASSERT_EQ(1, uid_eq(&NAME_$DATA.ndir_uid[i], &ARG_NODE));
        ASSERT_EQ(0, NAME_$DATA.wdir_mapped_info[i].active);
        ASSERT_EQ(0, NAME_$DATA.ndir_mapped_info[i].active);
    }

    /* map node dir into ASID 0, then /com */
    ASSERT_EQ(2, map_calls);
    ASSERT_EQ(1, uid_eq(&map_uid_seen[0], &ARG_NODE));
    ASSERT_EQ(0, map_asid_seen[0]);
    ASSERT_EQ((uintptr_t)&NAME_$DATA.node_mapped_info, (uintptr_t)map_info_seen[0]);
    ASSERT_EQ(1, uid_eq(&map_uid_seen[1], &COM_UID));
    ASSERT_EQ((uintptr_t)&NAME_$DATA.com_mapped_info, (uintptr_t)map_info_seen[1]);
    ASSERT_EQ(1, uid_eq(&NAME_$DATA.com_uid, &COM_UID));

    /* "/com" padded with spaces, length 4 */
    ASSERT_EQ(2, resolve_calls);
    ASSERT_EQ(4, resolve_len_seen[0]);
    ASSERT_EQ(0, strcmp("/com", resolve_path_seen[0]));
    ASSERT_EQ((uintptr_t)&NAME_$DATA.com_uid, (uintptr_t)resolve_out_seen[0]);

    ASSERT_EQ(0, dirptr_calls);

    /* FORMATN: six arguments, max len 0x100, NODE_$ME and the zero cell */
    ASSERT_EQ(1, formatn_calls);
    ASSERT_EQ((uintptr_t)name_$init_node_data_fmt_00e318d8, (uintptr_t)formatn_fmt_seen);
    ASSERT_EQ((uintptr_t)name_$init_path_buf, (uintptr_t)formatn_buf_seen);
    ASSERT_EQ(0x100, formatn_maxlen_seen);
    ASSERT_EQ((uintptr_t)&NODE_$ME, (uintptr_t)formatn_arg5_seen);
    ASSERT_EQ((uintptr_t)&name_$init_zero_l_00e31620, (uintptr_t)formatn_arg6_seen);

    /* characters 16..23 lower-cased (0x00E31826 loop), length kept at 23 */
    ASSERT_EQ(23, resolve_len_seen[1]);
    ASSERT_EQ(0, strcmp("/sys/node_data.0000abcd", resolve_path_seen[1]));
    ASSERT_EQ((uintptr_t)&NAME_$DATA.node_data_uid, (uintptr_t)resolve_out_seen[1]);
    ASSERT_EQ(1, uid_eq(&NAME_$DATA.node_data_uid, &ND_UID));

    ASSERT_EQ(0, crash_calls);
    ASSERT_EQ(0, write10_calls);
    ASSERT_EQ(1, exit_super_calls);
}

/* NIL root: VTOC supplies the UIDs, SET_DIRPTR runs, name cut to 14. */
TEST(vtoc_uids_path)
{
    uid_t nil = { 0, 0 };
    uid_t node = ARG_NODE;   /* ignored when root is NIL */

    reset();
    NAME_$INIT(&nil, &node);

    ASSERT_EQ(1, vtoc_calls);
    ASSERT_EQ(3, vtoc_volx_seen);
    ASSERT_EQ(1, uid_eq(&NAME_$DATA.root_uid, &VTOC_ROOT));
    ASSERT_EQ(1, uid_eq(&NAME_$DATA.node_uid, &VTOC_NODE));
    ASSERT_EQ(1, uid_eq(&NAME_$DATA.wdir_uid[57], &VTOC_NODE));

    ASSERT_EQ(1, dirptr_calls);
    ASSERT_EQ(1, uid_eq(&dirptr_file_seen, &VTOC_NODE));
    /* NAME_$CANNED_ROOT_UID (0xE173E4) is stored with UID_CONST, i.e. in
     * image byte order, so compare against the cell rather than the literal */
    ASSERT_EQ(NAME_$CANNED_ROOT_UID.high, dirptr_dir_seen.high);
    ASSERT_EQ(NAME_$CANNED_ROOT_UID.low, dirptr_dir_seen.low);

    /* 0x00E31822: length forced to 14, buffer NOT lower-cased */
    ASSERT_EQ(14, resolve_len_seen[1]);
    ASSERT_EQ(0, strcmp("/sys/node_data", resolve_path_seen[1]));
    ASSERT_EQ('A', name_$init_path_buf[19]);
    ASSERT_EQ(0, crash_calls);
    ASSERT_EQ(1, exit_super_calls);
}

/* 0x00E3176C / 0x00E3178C: a failed /com resolve or map falls back to node */
TEST(com_fallback_on_resolve_failure)
{
    uid_t root = ARG_ROOT, node = ARG_NODE;

    reset();
    resolve_status[0] = 0x000E0007;
    NAME_$INIT(&root, &node);

    ASSERT_EQ(1, map_calls);                          /* /com never mapped */
    ASSERT_EQ(1, uid_eq(&NAME_$DATA.com_uid, &ARG_NODE));
    ASSERT_EQ(0xFF, (uint8_t)NAME_$DATA.com_mapped_info.active);
    ASSERT_EQ(0x1000, NAME_$DATA.com_mapped_info.first_base);   /* node's */
    ASSERT_EQ(0, crash_calls);
}

TEST(com_fallback_on_map_false)
{
    uid_t root = ARG_ROOT, node = ARG_NODE;

    reset();
    map_result[1] = 0;
    NAME_$INIT(&root, &node);

    ASSERT_EQ(2, map_calls);
    ASSERT_EQ(1, uid_eq(&NAME_$DATA.com_uid, &ARG_NODE));
    ASSERT_EQ(0x1000, NAME_$DATA.com_mapped_info.first_base);
}

/* name_$init_check_status with a non-zero LOW word: 3 prints, wait, crash */
TEST(check_status_prints_waits_and_crashes)
{
    uid_t root = ARG_ROOT, node = ARG_NODE;

    reset();
    map_status[0] = 0x000E0025;
    NAME_$INIT(&root, &node);

    ASSERT_EQ(3, write10_calls);
    ASSERT_EQ((uintptr_t)name_$init_unable_fmt_00e315f6, (uintptr_t)write10_fmt_seen[0]);
    ASSERT_EQ((uintptr_t)&name_$init_zero_l_00e31620, (uintptr_t)write10_arg1_seen[0]);
    ASSERT_EQ((uintptr_t)name_$init_msg_map_00e31904, (uintptr_t)write10_fmt_seen[1]);
    ASSERT_EQ((uintptr_t)name_$init_detail_fmt_00e31612, (uintptr_t)write10_fmt_seen[2]);
    ASSERT_EQ((uintptr_t)&name_$init_zero_l_00e31620, (uintptr_t)write10_arg1_seen[2]);
    ASSERT_EQ(1, wait_calls);
    ASSERT_EQ(0, wait_type_seen);
    ASSERT_EQ(0x28, wait_high_seen);
    ASSERT_EQ(0, wait_low_seen);
    ASSERT_EQ(1, crash_calls);
    ASSERT_EQ(0x000E0025, crash_status_seen);
}

/* 0x00E31580 `tst.w (-0x16,A2)`: a status with only its high word set is
 * NOT caught by the nested check ... */
TEST(check_status_ignores_high_word_only)
{
    uid_t root = ARG_ROOT, node = ARG_NODE;

    reset();
    map_status[0] = 0x00010000;
    NAME_$INIT(&root, &node);
    ASSERT_EQ(0, write10_calls);
    ASSERT_EQ(0, crash_calls);
}

/* ... but the extra `tst.l` after SET_DIRPTR (0x00E317E6) does catch it. */
TEST(set_dirptr_high_word_status_crashes_directly)
{
    uid_t nil = { 0, 0 };
    uid_t node = ARG_NODE;

    reset();
    dirptr_status = 0x00010000;
    NAME_$INIT(&nil, &node);
    ASSERT_EQ(0, write10_calls);
    ASSERT_EQ(1, crash_calls);
    ASSERT_EQ(0x00010000, crash_status_seen);
}

/* the final check passes the buffer and the length as "%a" arguments */
TEST(resolve_failure_reports_buffer_and_length)
{
    uid_t root = ARG_ROOT, node = ARG_NODE;

    reset();
    resolve_status[1] = 0x000E0007;
    NAME_$INIT(&root, &node);
    ASSERT_EQ(3, write10_calls);
    ASSERT_EQ((uintptr_t)name_$init_msg_resolve_00e3189c, (uintptr_t)write10_fmt_seen[1]);
    ASSERT_EQ((uintptr_t)name_$init_path_buf, (uintptr_t)write10_arg1_seen[2]);
    ASSERT_EQ(1, crash_calls);
    ASSERT_EQ(0x000E0007, crash_status_seen);
}

int main(void)
{
    printf("NAME_$INIT tests\n");
    RUN_TEST(supplied_uids_path);
    RUN_TEST(vtoc_uids_path);
    RUN_TEST(com_fallback_on_resolve_failure);
    RUN_TEST(com_fallback_on_map_false);
    RUN_TEST(check_status_prints_waits_and_crashes);
    RUN_TEST(check_status_ignores_high_word_only);
    RUN_TEST(set_dirptr_high_word_status_crashes_directly);
    RUN_TEST(resolve_failure_reports_buffer_and_length);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
