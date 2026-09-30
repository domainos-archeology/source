/*
 * acl/test/test_convert_funky_acl.c - unit tests for ACL_$CONVERT_FUNKY_ACL (0x00E4900C).
 */

#include <stdint.h>
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

#include "acl/acl_internal.h"
#include "dir/dir.h"

uid_t ACL_$FILE_ACL = { 0x00000444u, 0x0000A001u };
uid_t ACL_$DIR_ACL  = { 0x0000044Cu, 0x0000A002u };

static int n_attr, n_def;
static uint16_t attr_flags;
static uid_t attr_uid, def_uid;
static uint8_t loc_flags_seen;
static uint8_t attr_obj0, attr_obj1;
static status_$t attr_st, def_st;
static uid_t *def_type;
static void *def_buf;

void AST_$GET_ACL_ATTRIBUTES(file_$obj_loc_t *loc_rec, uint16_t flags,
                             ast_$acl_attr_t *acl, status_$t *status)
{
    int i;
    n_attr++; attr_flags = flags; attr_uid = loc_rec->uid;
    loc_flags_seen = (uint8_t)loc_rec->flags;
    acl->obj_flags[0] = attr_obj0;
    acl->obj_flags[1] = attr_obj1;
    acl->default_acl.high = 0xDEF00001u;
    acl->default_acl.low = 0x00000002u;
    for (i = 0; i < 44; i++) acl->acl_data[i] = (uint8_t)(0x40 + i);
    *status = attr_st;
}
void DIR_$GET_DEF_PROTECTION(uid_t *dir_uid, uid_t *acl_type, void *prot_buf,
                             uid_t *prot_uid, status_$t *status_ret)
{
    n_def++; def_uid = *dir_uid; def_type = acl_type; def_buf = prot_buf;
    prot_uid->high = 0x5555u;
    *status_ret = def_st;
}

#include "../convert_funky_acl.c"

static uid_t in, prot, target;
static uint8_t data[44];
static status_$t st;

static void reset(uint32_t low)
{
    n_attr = n_def = 0;
    attr_st = def_st = 0;
    in.high = 0x12345678u; in.low = low;
    memset(&prot, 0xAA, sizeof(prot));
    memset(&target, 0xAA, sizeof(target));
    memset(data, 0xAA, sizeof(data));
    st = 0x11111111;
}

/* 0x80: object attributes; bits 25..27 masked out of the UID handed on. */
TEST(object_file)
{
    reset(0x0E800123u);          /* 0x0E80 >> 4 = 0xE8, & 0xE0 = 0xE0: not 0x80 */
    ACL_$CONVERT_FUNKY_ACL(&in, data, &prot, &target, &st);
    ASSERT_EQ(0, n_attr);
    ASSERT_EQ(0, n_def);
    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(0xAAAAAAAAu, target.high);

    reset(0x0E080123u);          /* (0x0E08 & 0xFF0) >> 4 = 0xE0 & 0xE0 */
    ACL_$CONVERT_FUNKY_ACL(&in, data, &prot, &target, &st);
    ASSERT_EQ(0, n_attr);

    reset(0x08000123u | 0x0E000000u);
    in.low = 0x0F080123u;        /* ((0x0F08 & 0xFF0)>>4) = 0xF0 & 0xE0 = 0xE0 */
    ACL_$CONVERT_FUNKY_ACL(&in, data, &prot, &target, &st);
    ASSERT_EQ(0, n_attr);

    reset(0x08080123u);          /* (0x0808 & 0xFF0) >> 4 = 0x80 */
    attr_obj0 = 0; attr_obj1 = 0; attr_st = 0x00120034;
    ACL_$CONVERT_FUNKY_ACL(&in, data, &prot, &target, &st);
    ASSERT_EQ(1, n_attr);
    ASSERT_EQ(0x21, attr_flags);
    ASSERT_EQ(0x12345678u, attr_uid.high);
    ASSERT_EQ(0x00080123u, attr_uid.low);
    ASSERT_EQ(0, loc_flags_seen & 0x40);
    ASSERT_EQ(0x00120034, st);
    ASSERT_EQ(0x40, data[0]);
    ASSERT_EQ(0x40 + 43, data[43]);
    ASSERT_EQ(0xDEF00001u, prot.high);
    ASSERT_EQ(0x01000002u, prot.low);       /* obj_flags[0] == 0: bit 24 */
    ASSERT_EQ(0x444u, target.high);
}

TEST(object_directory)
{
    reset(0x08080000u);
    attr_obj0 = 1; attr_obj1 = 2;
    ACL_$CONVERT_FUNKY_ACL(&in, data, &prot, &target, &st);
    ASSERT_EQ(0x44Cu, target.high);
    ASSERT_EQ(0x00000002u, prot.low);       /* obj_flags[0] != 0 */

    reset(0x08080000u);
    attr_obj0 = 1; attr_obj1 = 1;
    ACL_$CONVERT_FUNKY_ACL(&in, data, &prot, &target, &st);
    ASSERT_EQ(0x44Cu, target.high);

    reset(0x08080000u);
    attr_obj1 = 3;
    ACL_$CONVERT_FUNKY_ACL(&in, data, &prot, &target, &st);
    ASSERT_EQ(0x444u, target.high);
}

TEST(default_file_and_dir)
{
    reset(0x04000000u);          /* word 0x0400: 0x40 */
    def_st = 0x77;
    ACL_$CONVERT_FUNKY_ACL(&in, data, &prot, &target, &st);
    ASSERT_EQ(1, n_def);
    ASSERT_EQ(0x00000000u, def_uid.low);   /* bit 26 masked */
    ASSERT_EQ((uintptr_t)&ACL_$FILE_ACL, (uintptr_t)def_type);
    ASSERT_EQ((uintptr_t)data, (uintptr_t)def_buf);
    ASSERT_EQ(0x5555u, prot.high);
    ASSERT_EQ(0x77, st);
    ASSERT_EQ(0x444u, target.high);

    reset(0x02000000u);          /* word 0x0200: 0x20 */
    ACL_$CONVERT_FUNKY_ACL(&in, data, &prot, &target, &st);
    ASSERT_EQ((uintptr_t)&ACL_$DIR_ACL, (uintptr_t)def_type);
    ASSERT_EQ(0x44Cu, target.high);
    ASSERT_EQ(0, n_attr);
}

int main(void)
{
    printf("ACL_$CONVERT_FUNKY_ACL tests\n");
    RUN_TEST(object_file);
    RUN_TEST(object_directory);
    RUN_TEST(default_file_and_dir);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
