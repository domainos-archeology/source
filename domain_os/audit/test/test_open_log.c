/*
 * audit/test/test_open_log.c - unit tests for audit_$open_log (0x00E716CC).
 *
 * audit/open_log.c is #included below with NAME_$RESOLVE, NAME_$CR_FILE,
 * FILE_$SET_TYPE, FILE_$GET_ATTRIBUTES, FILE_$PRIV_LOCK / FILE_$PRIV_UNLOCK
 * and MST_$MAPS mocked.  The tests pin down bead source-joeo: the backquote
 * path cell, the two FILE_$GET_ATTRIBUTES constant cells the C was passing as
 * NULL, and the file size coming from attribute-record offset 0x14.
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

#include "audit/audit_internal.h"
#include "name/name.h"
#include "file/file.h"
#include "ast/ast.h"
#include "mst/mst.h"
#include "uid/uid.h"

/* ------------------------------------------------------------------ */
/* Globals                                                             */
/* ------------------------------------------------------------------ */

audit_data_t AUDIT_$DATA;
uid_t UID_$NIL       = { 0, 0 };
uid_t UNSTRUCT_$UID  = { 0x000003C4u, 0x00000000u };  /* 0x00E173C4 */

/* ------------------------------------------------------------------ */
/* Mocks                                                               */
/* ------------------------------------------------------------------ */

static const char *resolve_path;
static int16_t     resolve_len;
static int         resolve_calls;
static status_$t   resolve_result;
static uid_t       resolve_uid;

void NAME_$RESOLVE(char *path, int16_t *path_len, uid_t *resolved_uid,
                   status_$t *status_ret)
{
    resolve_calls++;
    resolve_path = path;
    resolve_len  = *path_len;
    *resolved_uid = resolve_uid;
    *status_ret = resolve_result;
}

static const char *create_path;
static int16_t     create_len;
static int         create_calls;
static status_$t   create_result;

void NAME_$CR_FILE(char *path, int16_t *path_len, uid_t *file_ret,
                   status_$t *status_ret)
{
    create_calls++;
    create_path = path;
    create_len  = *path_len;
    *file_ret = resolve_uid;
    *status_ret = create_result;
}

static int   set_type_calls;
static uid_t set_type_uid;
void FILE_$SET_TYPE(uid_t *file_uid, uid_t *type_uid, status_$t *status_ret)
{
    set_type_calls++;
    set_type_uid = *type_uid;
    (void)file_uid;
    *status_ret = status_$ok;
}

/* FILE_$GET_ATTRIBUTES capture. */
static int              ga_calls;
static uint16_t         ga_request;
static int16_t          ga_size;
static file_$obj_loc_t *ga_loc;
static uint32_t         ga_length_to_report;

void FILE_$GET_ATTRIBUTES(uid_t *file_uid, void *param_2, int16_t *size_ptr,
                          file_$obj_loc_t *loc_rec, void *attr_out,
                          status_$t *status_ret)
{
    ga_calls++;
    ga_request = *(const uint16_t *)param_2;
    ga_size    = *size_ptr;
    ga_loc     = loc_rec;
    (void)file_uid;

    /* Fill the whole 0x90-byte record with a pattern, then the length. */
    memset(attr_out, 0xAB, AST_ATTR_REC_SIZE);
    *(uint32_t *)(void *)((uint8_t *)attr_out + 0x14) = ga_length_to_report;
    *status_ret = status_$ok;
}

static int         lock_calls;
static const void *lock_acl_ctx;
static uint16_t    lock_side;
static uint16_t    lock_mode;
static status_$t   lock_result;

void FILE_$PRIV_LOCK(uid_t *file_uid, int16_t asid, uint16_t side,
                     uint16_t lock_mode_arg, boolean local_only,
                     uint16_t flags, uint16_t key,
                     uint32_t rem_key, uint32_t rem_node, uint32_t rem_extra,
                     void **acl_ctx, uint16_t rem_wait,
                     uint32_t *slot_io, uint16_t *rights_out,
                     status_$t *status_ret)
{
    lock_calls++;
    lock_acl_ctx = acl_ctx;
    lock_side = side;
    lock_mode = lock_mode_arg;
    (void)file_uid; (void)asid; (void)local_only; (void)flags; (void)key;
    (void)rem_key; (void)rem_node; (void)rem_extra; (void)rem_wait;
    *slot_io = 0x5A5A;
    *rights_out = 0;
    *status_ret = lock_result;
}

static int unlock_calls;
boolean FILE_$PRIV_UNLOCK(uid_t *file_uid, int32_t lock_slot,
                          uint16_t mode, uint16_t asid,
                          boolean by_key, uint16_t key,
                          uint32_t rem_key, uint32_t rem_node,
                          uint32_t *dtv_out, status_$t *status_ret)
{
    unlock_calls++;
    (void)file_uid; (void)lock_slot; (void)mode; (void)asid; (void)by_key;
    (void)key; (void)rem_key; (void)rem_node;
    *dtv_out = 0;
    *status_ret = status_$ok;
    return 0;
}

static uint8_t    map_area[64];
static int        maps_calls;
static uint32_t   maps_start_va;
static uint32_t   maps_length;
static status_$t  maps_result;

void *MST_$MAPS(int16_t asid, boolean direction, uid_t *uid, uint32_t start_va,
                uint32_t length, int16_t area_id, uint32_t area_size,
                boolean access_rights, void *map_info,
                status_$t *status)
{
    maps_calls++;
    maps_start_va = start_va;
    maps_length = length;
    (void)asid; (void)direction; (void)uid; (void)area_id; (void)area_size;
    (void)access_rights;
    *(uint32_t *)map_info = 0x1000;
    *status = maps_result;
    return map_area;
}

#include "../open_log.c"

/* ------------------------------------------------------------------ */

static status_$t run_open(status_$t resolve_st, status_$t create_st,
                          uint32_t reported_len)
{
    status_$t status = 0x7F7F7F7F;

    memset(&AUDIT_$DATA, 0, sizeof(AUDIT_$DATA));
    resolve_calls = create_calls = set_type_calls = 0;
    ga_calls = lock_calls = unlock_calls = maps_calls = 0;
    resolve_path = create_path = NULL;
    resolve_len = create_len = -1;
    ga_request = 0xFFFF;
    ga_size = -1;
    ga_loc = NULL;
    ga_length_to_report = reported_len;
    resolve_result = resolve_st;
    create_result = create_st;
    resolve_uid.high = 0x11112222u;
    resolve_uid.low  = 0x33334444u;
    lock_result = status_$ok;
    maps_result = status_$ok;

    audit_$open_log(&status);
    return status;
}

/*
 * 0x00E71844: 60 6e 6f 64 65 5f 64 61 74 61 2f ... - a BACKQUOTE, then
 * "node_data/audit/audit_log".  0x00E7183E holds 0x001A = 26.
 */
TEST(path_cell)
{
    ASSERT_EQ('`', log_path[0]);
    ASSERT_EQ(0, strcmp(log_path, "`node_data/audit/audit_log"));
    ASSERT_EQ(26, sizeof(log_path) - 1);
    ASSERT_EQ(0x001A, log_path_len);
    ASSERT_EQ(0x001A, sizeof(log_path) - 1);
}

/* 0x00E71840 = 0x0004, 0x00E71842 = 0x0090. */
TEST(attribute_cells)
{
    ASSERT_EQ(0x0004, attr_request);
    ASSERT_EQ(0x0090, attr_buf_size);
    ASSERT_EQ(0x90, AST_ATTR_REC_SIZE);
    ASSERT_EQ(0x14, AST_ATTR_OFF_LENGTH);
}

TEST(resolve_uses_the_image_path)
{
    run_open(status_$ok, status_$ok, 0x1234);

    ASSERT_EQ(1, resolve_calls);
    ASSERT_TRUE(resolve_path == log_path);
    ASSERT_EQ(0x001A, resolve_len);
    ASSERT_EQ(0, create_calls);
}

/* 0x00E71708: `cmpi.l #0xe0007` then NAME_$CR_FILE with the same cells. */
TEST(creates_when_not_found)
{
    run_open(status_$naming_name_not_found, status_$ok, 0);

    ASSERT_EQ(1, resolve_calls);
    ASSERT_EQ(1, create_calls);
    ASSERT_TRUE(create_path == log_path);
    ASSERT_EQ(0x001A, create_len);
}

/* 0x00E7174A-0x00E71766 hands over both cells, a loc record and the buffer. */
TEST(get_attributes_arguments)
{
    run_open(status_$ok, status_$ok, 0x1234);

    ASSERT_EQ(1, ga_calls);
    ASSERT_EQ(0x0004, ga_request);
    ASSERT_EQ(0x0090, ga_size);
    ASSERT_TRUE(ga_loc != NULL);
}

/* 0x00E71770: the length comes from attr+0x14, not attr+0x1C. */
TEST(file_size_comes_from_offset_0x14)
{
    run_open(status_$ok, status_$ok, 0xDEADBEEFu);
    ASSERT_EQ(0xDEADBEEFu, AUDIT_$DATA.file_offset);

    run_open(status_$ok, status_$ok, 0);
    ASSERT_EQ(0u, AUDIT_$DATA.file_offset);
    /* attr+0x1C still holds the 0xAB fill, so a wrong offset would show. */
}

/* 0x00E71784: the ACL-context argument is the address of a NIL cell. */
TEST(priv_lock_gets_the_nil_cell_address)
{
    run_open(status_$ok, status_$ok, 0x40);

    ASSERT_EQ(1, lock_calls);
    ASSERT_TRUE(lock_acl_ctx == (const void *)&audit_$open_log_nil_acl_ctx);
    ASSERT_EQ(1, lock_side);        /* pea (0x1).w -> asid 0, side 1 */
    ASSERT_EQ(4, lock_mode);        /* move.l #0x40000 -> mode 4 */
}

/* 0x00E717AC-0x00E717D8, then 0x00E71804-0x00E71812. */
TEST(maps_and_write_pointers)
{
    run_open(status_$ok, status_$ok, 0x2000);

    ASSERT_EQ(1, maps_calls);
    ASSERT_EQ(0x2000u, maps_start_va);
    ASSERT_EQ(0x8000u, maps_length);
    ASSERT_TRUE(AUDIT_$DATA.buffer_base == (void *)map_area);
    ASSERT_TRUE(AUDIT_$DATA.write_ptr == (void *)map_area);
    ASSERT_EQ(0, AUDIT_$DATA.dirty);
    ASSERT_EQ(0, unlock_calls);
}

/* 0x00E716D6-0x00E716EC: a non-NIL UID short-circuits. */
TEST(already_open_short_circuit)
{
    status_$t status = 0x7F7F7F7F;

    memset(&AUDIT_$DATA, 0, sizeof(AUDIT_$DATA));
    AUDIT_$DATA.log_file_uid.high = 1;
    resolve_calls = ga_calls = 0;

    audit_$open_log(&status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(0, resolve_calls);
    ASSERT_EQ(0, ga_calls);
}

/* 0x00E71818-0x00E71834 */
TEST(error_path_resets_state)
{
    status_$t st = run_open(0x00030001, status_$ok, 0);

    ASSERT_EQ(0x00030001, st);
    ASSERT_EQ(0u, AUDIT_$DATA.log_file_uid.high);
    ASSERT_EQ(0u, AUDIT_$DATA.log_file_uid.low);
    ASSERT_TRUE(AUDIT_$DATA.write_ptr == NULL);
    ASSERT_EQ(0u, AUDIT_$DATA.file_offset);
    ASSERT_EQ(0u, AUDIT_$DATA.bytes_remaining);
    ASSERT_EQ(0, AUDIT_$DATA.dirty);
}

int main(void)
{
    printf("audit_$open_log tests\n");

    RUN_TEST(path_cell);
    RUN_TEST(attribute_cells);
    RUN_TEST(resolve_uses_the_image_path);
    RUN_TEST(creates_when_not_found);
    RUN_TEST(get_attributes_arguments);
    RUN_TEST(file_size_comes_from_offset_0x14);
    RUN_TEST(priv_lock_gets_the_nil_cell_address);
    RUN_TEST(maps_and_write_pointers);
    RUN_TEST(already_open_short_circuit);
    RUN_TEST(error_path_resets_state);

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
