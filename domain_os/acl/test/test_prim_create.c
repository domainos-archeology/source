/*
 * acl/test/test_prim_create.c - unit tests for ACL_$PRIM_CREATE (0x00E47968).
 *
 * acl/prim_create.c is #included below with AST_$GET_ACL_ATTRIBUTES,
 * REM_FILE_$ACL_CREATE, FILE_$PRIV_CREATE, MST_$MAPS / MST_$UNMAP_PRIVI,
 * FILE_$MK_IMMUTABLE, AST_$PURIFY and acl_$prim_create_internal mocked.
 * The tests pin down bead source-1y86: the three arguments the C used to
 * pass as NULL are real frame cells and a real image constant.
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
#include "ast/ast.h"
#include "file/file.h"
#include "mst/mst.h"
#include "rem_file/rem_file.h"

/* ------------------------------------------------------------------ */

uint16_t PROC1_$CURRENT;
uint16_t PROC1_$AS_ID;
MODULE_DATA_DEFINE(acl_$unwired_data_t, ACL_$UNWIRED_DATA, 0x00E7CF54);
uid_t    UID_$NIL = { 0, 0 };

/* ------------------------------------------------------------------ */
/* Mocks                                                               */
/* ------------------------------------------------------------------ */

static ast_$acl_attr_t ga_attr_to_return;
static status_$t       ga_status;
static int             ga_calls;
static int8_t          ga_loc_flags;   /* what the callee leaves at +0x1D */

void AST_$GET_ACL_ATTRIBUTES(file_$obj_loc_t *loc_rec, uint16_t flags,
                             ast_$acl_attr_t *acl, status_$t *status)
{
    ga_calls++;
    (void)flags;
    /* 0x00E479CA reads the record's flags byte AFTER this call, so the real
     * routine writes the descriptor back; the mock does the same. */
    loc_rec->flags = ga_loc_flags;
    *acl = ga_attr_to_return;
    *status = ga_status;
}

static int         rem_calls;
static const void *rem_header;
void REM_FILE_$ACL_CREATE(void *addr_info, void *acl_data, void *acl_header,
                          uid_t *parent_uid, uid_t *acl_uid_out,
                          status_$t *status)
{
    rem_calls++;
    rem_header = acl_header;
    (void)addr_info; (void)acl_data; (void)parent_uid; (void)acl_uid_out;
    *status = status_$ok;
}

static int       create_calls;
static status_$t create_result;
uint32_t FILE_$PRIV_CREATE(int16_t file_type, const uid_t *type_uid,
                           uid_t *dir_uid, uid_t *file_uid_ret,
                           uint32_t initial_size, uint16_t flags,
                           uid_t *owner_info, status_$t *status_ret)
{
    create_calls++;
    (void)file_type; (void)type_uid; (void)dir_uid; (void)initial_size;
    (void)flags; (void)owner_info;
    file_uid_ret->high = 0xCAFE0000u;
    file_uid_ret->low  = 0x0000BABEu;
    *status_ret = create_result;
    return 0;
}

static uint8_t    page[0x400];
static int        maps_calls;
static void      *maps_map_info;
static status_$t  maps_result;

void *MST_$MAPS(int16_t asid, boolean direction, uid_t *uid, uint32_t start_va,
                uint32_t length, int16_t area_id, uint32_t area_size,
                boolean access_rights, void *map_info, status_$t *status)
{
    maps_calls++;
    maps_map_info = map_info;
    (void)asid; (void)direction; (void)uid; (void)start_va; (void)length;
    (void)area_id; (void)area_size; (void)access_rights;
    if (map_info != NULL) {
        *(uint32_t *)map_info = 0x400;
    }
    *status = maps_result;
    return page;
}

static int unmap_calls;
void MST_$UNMAP_PRIVI(int16_t mode, uid_t *uid, uint32_t start, uint32_t size,
                      uint16_t asid, status_$t *status_ret)
{
    unmap_calls++;
    (void)mode; (void)uid; (void)start; (void)size; (void)asid;
    *status_ret = status_$ok;
}

static int immutable_calls;
void FILE_$MK_IMMUTABLE(uid_t *file_uid, status_$t *status_ret)
{
    immutable_calls++;
    (void)file_uid;
    *status_ret = status_$ok;
}

static int              purify_calls;
static const uint32_t  *purify_segments;
static uint16_t         purify_flags;
uint16_t AST_$PURIFY(uid_t *uid, uint16_t flags, int16_t segment,
                     uint32_t *segment_list, uint16_t unused,
                     status_$t *status)
{
    purify_calls++;
    purify_segments = segment_list;
    purify_flags = flags;
    (void)uid; (void)segment; (void)unused;
    *status = status_$ok;
    return 0;
}

static int       internal_calls;
static int16_t  *internal_len_ret;
static void     *internal_header;
void acl_$prim_create_internal(void *acl_header, void *acl_data,
                               int16_t data_len, void *subsys_uid,
                               int8_t flag, void *image,
                               int16_t *image_len_ret, status_$t *status_ret)
{
    internal_calls++;
    internal_header = acl_header;
    internal_len_ret = image_len_ret;
    (void)acl_data; (void)data_len; (void)subsys_uid; (void)flag; (void)image;
    if (image_len_ret != NULL) {
        *image_len_ret = 0x34;
    }
    *status_ret = status_$ok;
}

#include "../prim_create.c"

/* ------------------------------------------------------------------ */

#define TEST_PID 3

static uint8_t acl_buf[0x100];
static uid_t   dir_uid = { 0x0D0D0D0Du, 0x0E0E0E0Eu };
static uint8_t header_obj[16];

static status_$t run_create(uint8_t obj_type, uint8_t flags_lo,
                            int8_t loc_flags, int16_t entries)
{
    status_$t status = 0x7F7F7F7F;
    uid_t file_uid;
    int16_t data_len;

    memset(acl_buf, 0, sizeof(acl_buf));
    memset(page, 0, sizeof(page));
    memset(ACL_$UNWIRED_DATA.super_count, 0, sizeof(ACL_$UNWIRED_DATA.super_count));
    memset(&ga_attr_to_return, 0, sizeof(ga_attr_to_return));

    PROC1_$CURRENT = TEST_PID;
    PROC1_$AS_ID = 1;

    ga_attr_to_return.obj_flags[ACL_ATTR_OBJ_TYPE] = obj_type;
    ga_attr_to_return.obj_flags[ACL_ATTR_FLAGS_LO] = flags_lo;
    ga_status = status_$ok;
    ga_loc_flags = loc_flags;
    ga_calls = 0;
    rem_calls = 0;
    rem_header = NULL;
    create_calls = 0;
    create_result = status_$ok;
    maps_calls = 0;
    maps_map_info = NULL;
    maps_result = status_$ok;
    unmap_calls = immutable_calls = purify_calls = internal_calls = 0;
    purify_segments = NULL;
    internal_len_ret = NULL;
    internal_header = NULL;

    /* acl_data+0x0E is the entry count; the length must be 0x34 + n*0x20. */
    *(int16_t *)(void *)(acl_buf + 0x0E) = entries;
    data_len = (int16_t)(0x34 + entries * 0x20);

    /* The location record's flags byte drives the remote test. */
    ACL_$PRIM_CREATE(acl_buf, &data_len, &dir_uid, header_obj, &file_uid,
                     &status);
    return status;
}

/* 0x00E47B74: longword 0. */
TEST(purify_segment_cell)
{
    ASSERT_EQ(0u, acl_$prim_create_purify_segments);
    ASSERT_EQ(4, sizeof(acl_$prim_create_purify_segments));
}

/* 0x00E47A78: MST_$MAPS' map-info argument is a frame cell, not NULL. */
TEST(mst_maps_gets_a_real_out_cell)
{
    run_create(1, ACL_ATTR_FLAG_LOCAL, 0x00, 1);

    ASSERT_EQ(1, maps_calls);
    ASSERT_TRUE(maps_map_info != NULL);
}

/* 0x00E47AB4: the internal helper's image-length output is a frame cell. */
TEST(internal_helper_gets_a_real_out_cell)
{
    /* obj_flags[ACL_ATTR_OBJ_TYPE] == 0 selects the helper (0x00E47AAA). */
    run_create(0, ACL_ATTR_FLAG_LOCAL, 0x00, 1);

    ASSERT_EQ(1, internal_calls);
    ASSERT_TRUE(internal_len_ret != NULL);
    ASSERT_EQ(0x34, *internal_len_ret);
    /* 0x00E47AC8 hands the caller's own A6+0x14 argument straight through. */
    ASSERT_TRUE(internal_header == (void *)header_obj);
}

/* 0x00E47B3A: AST_$PURIFY's segment list is the address of that cell. */
TEST(purify_gets_the_cell_address)
{
    run_create(1, ACL_ATTR_FLAG_LOCAL, 0x00, 1);

    ASSERT_EQ(1, purify_calls);
    ASSERT_TRUE(purify_segments ==
                (const uint32_t *)&acl_$prim_create_purify_segments);
    ASSERT_EQ(2, purify_flags);      /* move.l #0x20000 -> flags 2 */
}

/* 0x00E479EA-0x00E479F8 and 0x00E47B5A-0x00E47B68. */
TEST(super_count_is_balanced)
{
    run_create(1, ACL_ATTR_FLAG_LOCAL, 0x00, 1);
    ASSERT_EQ(0, ACL_$UNWIRED_DATA.super_count[TEST_PID]);
    ASSERT_EQ(1, create_calls);
    ASSERT_EQ(1, unmap_calls);
    ASSERT_EQ(1, immutable_calls);
}

/* 0x00E47A00-0x00E47A1C: `data_len != 0x34 + entries*0x20` is refused. */
TEST(length_check)
{
    status_$t status = 0x7F7F7F7F;
    uid_t file_uid;
    int16_t data_len = 0x34;   /* but the buffer claims 2 entries */

    memset(acl_buf, 0, sizeof(acl_buf));
    memset(ACL_$UNWIRED_DATA.super_count, 0, sizeof(ACL_$UNWIRED_DATA.super_count));
    memset(&ga_attr_to_return, 0, sizeof(ga_attr_to_return));
    ga_attr_to_return.obj_flags[ACL_ATTR_FLAGS_LO] = ACL_ATTR_FLAG_LOCAL;
    ga_status = status_$ok;
    PROC1_$CURRENT = TEST_PID;
    create_calls = 0;
    *(int16_t *)(void *)(acl_buf + 0x0E) = 2;

    ACL_$PRIM_CREATE(acl_buf, &data_len, &dir_uid, header_obj, &file_uid,
                     &status);

    ASSERT_EQ(status_$image_buffer_too_small, status);
    ASSERT_EQ(0, create_calls);
    ASSERT_EQ(0, ACL_$UNWIRED_DATA.super_count[TEST_PID]);
}

/*
 * 0x00E479C2-0x00E479E6.  "not local" (obj_flags[3] bit 0 clear) AND a
 * negative location-record flags byte take the remote path, which hands
 * REM_FILE_$ACL_CREATE the caller's own A6+0x14 argument as acl_header.
 */
TEST(remote_path_passes_the_header_pointer)
{
    run_create(1, 0, (int8_t)0x80, 1);

    ASSERT_EQ(1, rem_calls);
    ASSERT_TRUE(rem_header == (const void *)header_obj);
    ASSERT_EQ(0, create_calls);
    /* 0x00E479E6 branches past the super-count bracket entirely. */
    ASSERT_EQ(0, ACL_$UNWIRED_DATA.super_count[TEST_PID]);
    ASSERT_EQ(0, maps_calls);
}

/* The same "not local" attribute with a non-negative flags byte stays local. */
TEST(local_path_when_flags_byte_is_positive)
{
    run_create(1, 0, 0x00, 1);

    ASSERT_EQ(0, rem_calls);
    ASSERT_EQ(1, create_calls);
    ASSERT_EQ(1, maps_calls);
}

int main(void)
{
    printf("ACL_$PRIM_CREATE tests\n");

    RUN_TEST(purify_segment_cell);
    RUN_TEST(mst_maps_gets_a_real_out_cell);
    RUN_TEST(internal_helper_gets_a_real_out_cell);
    RUN_TEST(purify_gets_the_cell_address);
    RUN_TEST(super_count_is_balanced);
    RUN_TEST(length_check);
    RUN_TEST(remote_path_passes_the_header_pointer);
    RUN_TEST(local_path_when_flags_byte_is_positive);

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
