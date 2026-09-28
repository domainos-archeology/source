/*
 * log/test/test_init.c - LOG_$INIT (0x00E30048)
 *
 * NAME_$RESOLVE / NAME_$CR_FILE / AST_$GET_COMMON_ATTRIBUTES / MST_$MAPS /
 * FILE_$LOCK / MST_$WIRE / LOG_$ADD and VFMT_$WRITE10 are mocked; the real
 * log_$check_op_status runs.  Checks the constant cells each callee gets
 * (path 0x00E30020, word length 0x00E3022A, lock cells 0x00E30238..3C),
 * the FILE_$LOCK mode 4, the page initialisation, the replay of the two
 * low-memory records, and where each failure stops.
 */

#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include "log/log_internal.h"
#include "name/name.h"
#include "ast/ast.h"
#include "mst/mst.h"
#include "file/file.h"

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
    long long _e = (long long)(expected); \
    long long _a = (long long)(actual); \
    if (_e != _a) { \
        printf("FAILED\n    Expected: %lld, Got: %lld at line %d\n", \
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

/* ==========================================================================
 * Mocks
 * ========================================================================== */

static int16_t page[0x200];

static status_$t resolve_status, create_status, cattr_status, maps_status,
                 lock_status, wire_status;
static uint32_t cattr_length;

static int resolve_calls, create_calls, cattr_calls, maps_calls, lock_calls,
           wire_calls, add_calls, write_calls;
static char *resolve_path, *create_path;
static int16_t *resolve_len, *create_len;
static uid_t *resolve_uid;
static uint16_t cattr_selector;
static uid_t cattr_uid;
static int8_t cattr_flags;
static int8_t maps_rights;
static boolean maps_direction;
static uint32_t maps_length;
static int16_t maps_area;
static const uint16_t *lock_index_ptr, *lock_mode_ptr;
static const uint8_t *lock_rights_ptr;
static void *lock_info_ptr;
static uint32_t wire_va;
static int16_t add_type[4], add_len[4];
static void *add_data[4];

void NAME_$RESOLVE(char *path, int16_t *path_len, uid_t *resolved_uid,
                   status_$t *status_ret)
{
    resolve_calls++;
    resolve_path = path;
    resolve_len = path_len;
    resolve_uid = resolved_uid;
    resolved_uid->high = 0x1234; resolved_uid->low = 0x5678;
    *status_ret = resolve_status;
}

void NAME_$CR_FILE(char *path, int16_t *path_len, uid_t *file_ret,
                   status_$t *status_ret)
{
    create_calls++;
    create_path = path;
    create_len = path_len;
    file_ret->high = 0xABCD; file_ret->low = 0xEF01;
    *status_ret = create_status;
}

void AST_$GET_COMMON_ATTRIBUTES(file_$obj_loc_t *loc_rec, uint16_t flags,
                                ast_$common_attr_t *attrs, status_$t *status)
{
    cattr_calls++;
    cattr_selector = flags;
    cattr_uid = loc_rec->uid;
    cattr_flags = loc_rec->flags;
    memset(attrs, 0, sizeof(*attrs));
    attrs->length = cattr_length;
    *status = cattr_status;
}

void *MST_$MAPS(int16_t asid, boolean direction, uid_t *uid, uint32_t start_va,
                uint32_t length, int16_t area_id, uint32_t area_size,
                boolean access_rights, void *map_info, status_$t *status)
{
    (void)asid; (void)uid; (void)start_va; (void)area_size; (void)map_info;
    maps_calls++;
    maps_direction = direction;
    maps_length = length;
    maps_area = area_id;
    maps_rights = (int8_t)access_rights;
    *status = maps_status;
    return page;
}

void FILE_$LOCK(uid_t *file_uid, const uint16_t *lock_index,
                const uint16_t *lock_mode, const uint8_t *rights,
                void *lock_info, status_$t *status_ret)
{
    (void)file_uid;
    lock_calls++;
    lock_index_ptr = lock_index;
    lock_mode_ptr = lock_mode;
    lock_rights_ptr = rights;
    lock_info_ptr = lock_info;
    *status_ret = lock_status;
}

uint32_t MST_$WIRE(uint32_t vpn, status_$t *status_ret)
{
    wire_calls++;
    wire_va = vpn;
    *status_ret = wire_status;
    return 0x00A5A5A5;
}

static int16_t fake_entry[8];

void LOG_$ADD(int16_t type, void *data, int16_t data_len)
{
    if (add_calls < 4) {
        add_type[add_calls] = type;
        add_data[add_calls] = data;
        add_len[add_calls] = data_len;
    }
    add_calls++;
    LOG_$STATE.current_entry_ptr = fake_entry;
}

void VFMT_$WRITE10(const char *format, ...)
{
    (void)format;
    write_calls++;
}

/* ==========================================================================
 * Code under test
 * ========================================================================== */

#include "../log_data.c"
#include "../check_op_status.c"
#include "../init.c"

static void reset(void)
{
    memset(page, 0, sizeof(page));
    memset(&LOG_$STATE, 0, sizeof(LOG_$STATE));
    memset(&LOG_$LAST_ENTRY, 0, sizeof(LOG_$LAST_ENTRY));
    memset(&CRASH_$RECORD, 0, sizeof(CRASH_$RECORD));
    memset(fake_entry, 0, sizeof(fake_entry));
    resolve_status = create_status = cattr_status = maps_status =
        lock_status = wire_status = status_$ok;
    cattr_length = 0x400;
    resolve_calls = create_calls = cattr_calls = maps_calls = lock_calls =
        wire_calls = add_calls = write_calls = 0;
}

/* ==========================================================================
 * Tests
 * ========================================================================== */

/* 0x00E30050-0x00E30068: the shared path cell and its WORD length. */
TEST(resolve_arguments)
{
    reset();
    LOG_$INIT();
    ASSERT_EQ(1, resolve_calls);
    ASSERT_PTR_EQ(log_$logfile_path, resolve_path);
    ASSERT_PTR_EQ(&log_$logfile_path_len, resolve_len);
    ASSERT_EQ(36, *resolve_len);
    ASSERT_EQ(36, strlen(resolve_path));
    ASSERT_PTR_EQ(&LOG_$STATE.logfile_uid, resolve_uid);
    ASSERT_EQ(0, create_calls);
}

/* 0x00E3006C-0x00E30088: name not found -> create with the same cells. */
TEST(missing_file_is_created)
{
    reset();
    resolve_status = status_$naming_name_not_found;
    LOG_$INIT();
    ASSERT_EQ(1, create_calls);
    ASSERT_PTR_EQ(log_$logfile_path, create_path);
    ASSERT_PTR_EQ(&log_$logfile_path_len, create_len);
    ASSERT_EQ(0xEF01, LOG_$STATE.logfile_uid.low);
    ASSERT_EQ(0, write_calls);
    ASSERT_EQ(1, wire_calls);
}

/* A failing create prints (3 VFMT calls) and stops before the attributes. */
TEST(create_failure_stops)
{
    reset();
    resolve_status = status_$naming_name_not_found;
    create_status = 0x00070002;
    LOG_$INIT();
    ASSERT_EQ(3, write_calls);
    ASSERT_EQ(0, cattr_calls);
    ASSERT_PTR_EQ(NULL, LOG_$LOGFILE_PTR);
}

/* Any other resolve failure stops at "resolve". */
TEST(resolve_failure_stops)
{
    reset();
    resolve_status = 0x000E0002;
    LOG_$INIT();
    ASSERT_EQ(0, create_calls);
    ASSERT_EQ(3, write_calls);
    ASSERT_EQ(0, cattr_calls);
}

/* 0x00E300B2-0x00E300D8: UID at desc+8, bit 6 of desc+0x1D cleared,
 * selector 2. */
TEST(attribute_call)
{
    reset();
    LOG_$INIT();
    ASSERT_EQ(1, cattr_calls);
    ASSERT_EQ(2, cattr_selector);
    ASSERT_EQ(0x5678, cattr_uid.low);
    ASSERT_EQ(0, cattr_flags & 0x40);
}

/* 0x00E300F8-0x00E30122: direction TRUE, length 0x400, area 0x16, rights =
 * the "empty file" boolean. */
TEST(map_arguments)
{
    reset();
    LOG_$INIT();
    ASSERT_EQ(1, maps_calls);
    ASSERT_EQ(-1, (int8_t)maps_direction);
    ASSERT_EQ(0x400, maps_length);
    ASSERT_EQ(0x16, maps_area);
    ASSERT_EQ(0, maps_rights);

    reset();
    cattr_length = 0;
    LOG_$INIT();
    ASSERT_EQ(-1, maps_rights);
}

/* 0x00E30134-0x00E30154: FILE_$LOCK's three cells - index 0, MODE 4,
 * rights 0 - and a real output record. */
TEST(lock_arguments)
{
    reset();
    LOG_$INIT();
    ASSERT_EQ(1, lock_calls);
    ASSERT_PTR_EQ(&log_$lock_index, lock_index_ptr);
    ASSERT_PTR_EQ(&log_$lock_mode, lock_mode_ptr);
    ASSERT_PTR_EQ(&log_$lock_rights, lock_rights_ptr);
    ASSERT_EQ(0, *lock_index_ptr);
    ASSERT_EQ(4, *lock_mode_ptr);
    ASSERT_EQ(0, *lock_rights_ptr);
    ASSERT_EQ(1, lock_info_ptr != NULL);
}

/* 0x00E30168-0x00E30186: an empty file, or a page with both index words
 * zero, is set to head 0 / tail 1 and the log is dirty; otherwise the page
 * is trusted. */
TEST(page_initialisation)
{
    reset();
    page[0] = 0; page[1] = 0;
    LOG_$INIT();
    ASSERT_EQ(0, page[0]);
    ASSERT_EQ(1, page[1]);
    ASSERT_EQ(-1, (int8_t)LOG_$STATE.dirty_flag);

    reset();
    page[0] = 0x40; page[1] = 0x88;
    LOG_$INIT();
    ASSERT_EQ(0x40, page[0]);
    ASSERT_EQ(0x88, page[1]);
    ASSERT_EQ(0, LOG_$STATE.dirty_flag);

    reset();
    cattr_length = 0;                   /* empty file: reset regardless */
    page[0] = 0x40; page[1] = 0x88;
    LOG_$INIT();
    ASSERT_EQ(0, page[0]);
    ASSERT_EQ(1, page[1]);
}

/* 0x00E3018A-0x00E301B6: wire the page, keep the handle, publish the
 * pointer only after the wire succeeded. */
TEST(wire_and_publish)
{
    reset();
    LOG_$INIT();
    ASSERT_EQ(ARCH_PTR_TO_VA(page), wire_va);
    ASSERT_EQ(0x00A5A5A5, LOG_$STATE.wired_handle);
    ASSERT_PTR_EQ(page, LOG_$LOGFILE_PTR);

    reset();
    wire_status = 0x00040003;
    LOG_$INIT();
    ASSERT_EQ(0x00A5A5A5, LOG_$STATE.wired_handle);
    ASSERT_PTR_EQ(NULL, LOG_$LOGFILE_PTR);
    ASSERT_EQ(0, add_calls);
}

/* 0x00E30212-0x00E3021A: with nothing pending, exactly one LOG_$ADD - the
 * type-0 init entry with the zero cell and length 0. */
TEST(init_entry_only)
{
    reset();
    LOG_$INIT();
    ASSERT_EQ(1, add_calls);
    ASSERT_EQ(0, add_type[0]);
    ASSERT_PTR_EQ(&LOG_$VFMT_NO_ARG, add_data[0]);
    ASSERT_EQ(0, add_len[0]);
}

/* 0x00E301BA-0x00E30210: both records pending -> last entry (with its own
 * timestamp restored), crash record (type 5, 8 bytes), init entry; both
 * magics cleared. */
TEST(pending_records_are_replayed)
{
    reset();
    LOG_$LAST_ENTRY.magic = LOG_PENDING_MAGIC;
    LOG_$LAST_ENTRY.size = 6;
    LOG_$LAST_ENTRY.type = 0x22;
    LOG_$LAST_ENTRY.timestamp = 0xDEADBEEFu;
    CRASH_$RECORD.magic = LOG_PENDING_MAGIC;
    LOG_$INIT();

    ASSERT_EQ(3, add_calls);
    ASSERT_EQ(0x22, add_type[0]);
    ASSERT_PTR_EQ(LOG_$LAST_ENTRY.data, add_data[0]);
    ASSERT_EQ(6, add_len[0]);
    ASSERT_EQ(0xDEADBEEFu, ((log_entry_header_t *)fake_entry)->timestamp);
    ASSERT_EQ(0, LOG_$LAST_ENTRY.magic);
    ASSERT_EQ(5, add_type[1]);
    ASSERT_PTR_EQ(CRASH_$RECORD.data, add_data[1]);
    ASSERT_EQ(8, add_len[1]);
    ASSERT_EQ(0, CRASH_$RECORD.magic);
    ASSERT_EQ(0, add_type[2]);
}

int main(void)
{
    printf("LOG_$INIT tests\n");
    RUN_TEST(resolve_arguments);
    RUN_TEST(missing_file_is_created);
    RUN_TEST(create_failure_stops);
    RUN_TEST(resolve_failure_stops);
    RUN_TEST(attribute_call);
    RUN_TEST(map_arguments);
    RUN_TEST(lock_arguments);
    RUN_TEST(page_initialisation);
    RUN_TEST(wire_and_publish);
    RUN_TEST(init_entry_only);
    RUN_TEST(pending_records_are_replayed);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
