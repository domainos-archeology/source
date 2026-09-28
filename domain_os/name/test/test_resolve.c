/*
 * name/test/test_resolve.c - unit tests for name_$parse_component
 * (0x00E4A004), name_$resolve_internal (0x00E4A060) and NAME_$RESOLVE
 * (0x00E4A258).
 *
 * The real name/resolve.c is #included.  NAME_$VALIDATE, NAME_$GET_NODE_UID,
 * NAME_$GET_NODE_DATA_UID and DIR_$GET_ENTRYU are mocked; the directory mock
 * answers from a small scripted table keyed by component name.
 */

#include <stdio.h>
#include <string.h>

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

uid_t UID_$NIL = { 0, 0 };

/* ------------------------------------------------------------------ */
/* Mocks                                                                */
/* ------------------------------------------------------------------ */

static const uid_t NODE_UID      = { 0x11111111, 0x00000001 };
static const uid_t NODE_DATA_UID = { 0x22222222, 0x00000002 };

/* NAME_$VALIDATE: scripted result */
static start_path_type_t validate_type;
static int16_t           validate_consumed;
static int               validate_calls;
static uint16_t          validate_len_seen;

boolean NAME_$VALIDATE(char *path, uint16_t *path_len, int16_t *consumed,
                       start_path_type_t *start_path_type)
{
    (void)path;
    validate_calls++;
    validate_len_seen = *path_len;
    *consumed = validate_consumed;
    *start_path_type = validate_type;
    return (boolean)-1;
}

static int get_node_calls;
static int get_node_data_calls;

void NAME_$GET_NODE_UID(uid_t *node_uid)
{
    get_node_calls++;
    *node_uid = NODE_UID;
}

void NAME_$GET_NODE_DATA_UID(uid_t *node_data_uid)
{
    get_node_data_calls++;
    *node_data_uid = NODE_DATA_UID;
}

/* DIR_$GET_ENTRYU: table of (name -> type, uid, status) */
struct dir_answer {
    const char *name;
    int16_t     type;
    uid_t       uid;
    status_$t   status;
};
static struct dir_answer dir_table[8];
static int               dir_table_n;
static int               dir_calls;
static uid_t             dir_uid_seen[8];
static char              dir_name_seen[8][32];
static uint16_t          dir_len_seen[8];

void DIR_$GET_ENTRYU(uid_t *dir_uid, char *name, uint16_t *name_len,
                     void *entry_ret, status_$t *status_ret)
{
    int n = dir_calls++;
    int i;
    struct { int16_t type; uid_t uid; } __attribute__((packed, aligned(2))) *e = entry_ret;

    dir_uid_seen[n] = *dir_uid;
    memcpy(dir_name_seen[n], name, *name_len);
    dir_name_seen[n][*name_len] = 0;
    dir_len_seen[n] = *name_len;

    for (i = 0; i < dir_table_n; i++) {
        if (strlen(dir_table[i].name) == *name_len &&
            memcmp(dir_table[i].name, name, *name_len) == 0) {
            e->type = dir_table[i].type;
            e->uid = dir_table[i].uid;
            *status_ret = dir_table[i].status;
            return;
        }
    }
    e->type = 0;
    e->uid = UID_$NIL;
    *status_ret = status_$ok;
}

static void add_answer(const char *name, int16_t type, uint32_t hi, uint32_t lo, status_$t st)
{
    dir_table[dir_table_n].name = name;
    dir_table[dir_table_n].type = type;
    dir_table[dir_table_n].uid.high = hi;
    dir_table[dir_table_n].uid.low = lo;
    dir_table[dir_table_n].status = st;
    dir_table_n++;
}

static void reset(void)
{
    validate_type = start_path_$absolute;
    validate_consumed = 0;
    validate_calls = 0;
    get_node_calls = 0;
    get_node_data_calls = 0;
    dir_table_n = 0;
    dir_calls = 0;
    memset(dir_name_seen, 0, sizeof(dir_name_seen));
}

#include "../resolve.c"

static int uid_eq(const uid_t *a, uint32_t hi, uint32_t lo)
{
    return a->high == hi && a->low == lo;
}

/* ------------------------------------------------------------------ */
/* name_$parse_component                                                */
/* ------------------------------------------------------------------ */

TEST(parse_skips_slashes_and_counts_component)
{
    uint16_t next = 0, start = 0;
    int16_t len = -1;

    name_$parse_component("//abc/d", 7, 1, &next, &start, &len);
    ASSERT_EQ(3, start);
    ASSERT_EQ(3, len);
    ASSERT_EQ(6, next);         /* on the '/' after "abc" */

    name_$parse_component("//abc/d", 7, 6, &next, &start, &len);
    ASSERT_EQ(7, start);
    ASSERT_EQ(1, len);
    ASSERT_EQ(8, next);
}

/* 0x00E4A036-0x00E4A03A: only slashes left -> comp_len 0, comp_start untouched */
TEST(parse_end_of_path)
{
    uint16_t next = 0, start = 99;
    int16_t len = -1;

    name_$parse_component("abc/", 4, 4, &next, &start, &len);
    ASSERT_EQ(0, len);
    ASSERT_EQ(5, next);
    ASSERT_EQ(99, start);

    name_$parse_component("abc", 3, 4, &next, &start, &len);
    ASSERT_EQ(0, len);
    ASSERT_EQ(4, next);
    ASSERT_EQ(99, start);
}

/* ------------------------------------------------------------------ */
/* name_$resolve_internal                                               */
/* ------------------------------------------------------------------ */

/* 0x00E4A0CE: absolute -> cursor 2, node UID; walk "/a/b". */
TEST(absolute_walks_from_node_dir)
{
    uid_t dir, file;
    status_$t st = 0x77;

    reset();
    validate_type = start_path_$absolute;
    validate_consumed = 99;                   /* overridden by the arm */
    add_answer("a", 1, 0xAAAA, 1, status_$ok);
    add_answer("b", 1, 0xBBBB, 2, status_$ok);

    name_$resolve_internal("/a/b", 4, &dir, &file, &st);

    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(1, get_node_calls);
    ASSERT_EQ(2, dir_calls);
    ASSERT_EQ(1, uid_eq(&dir_uid_seen[0], NODE_UID.high, NODE_UID.low));
    ASSERT_EQ(0, strcmp("a", dir_name_seen[0]));
    ASSERT_EQ(1, uid_eq(&dir_uid_seen[1], 0xAAAA, 1));
    ASSERT_EQ(0, strcmp("b", dir_name_seen[1]));
    ASSERT_EQ(1, uid_eq(&dir, 0xAAAA, 1));
    ASSERT_EQ(1, uid_eq(&file, 0xBBBB, 2));
}

/* 0x00E4A0C4 index 0 -> 0x00E4A1D2: relative paths are invalid. */
TEST(relative_is_invalid)
{
    uid_t dir, file;
    status_$t st = 0;

    reset();
    validate_type = start_path_$relative;
    name_$resolve_internal("a", 1, &dir, &file, &st);
    ASSERT_EQ(status_$naming_invalid_pathname, st);
    ASSERT_EQ(0, dir_calls);
    ASSERT_EQ(1, uid_eq(&dir, 0, 0));
    ASSERT_EQ(1, uid_eq(&file, 0, 0));
}

TEST(network_is_invalid)
{
    uid_t dir, file;
    status_$t st = 0;

    reset();
    validate_type = start_path_$network;
    name_$resolve_internal("//x/y", 5, &dir, &file, &st);
    ASSERT_EQ(status_$naming_invalid_pathname, st);
    ASSERT_EQ(0, dir_calls);
}

/* 0x00E4A0EE-0x00E4A11A: node_data cursor 11 vs 12 */
TEST(node_data_cursor_11_or_12)
{
    uid_t dir, file;
    status_$t st = 0;

    /* "`node_data" exactly: cursor 11 -> no component -> node_data uid */
    reset();
    validate_type = start_path_$node_data;
    name_$resolve_internal("`node_data", 10, &dir, &file, &st);
    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(1, get_node_data_calls);
    ASSERT_EQ(0, dir_calls);
    ASSERT_EQ(1, uid_eq(&file, NODE_DATA_UID.high, NODE_DATA_UID.low));

    /* "`node_data/x": cursor 12 -> component "x" */
    reset();
    validate_type = start_path_$node_data;
    add_answer("x", 1, 0x5555, 5, status_$ok);
    name_$resolve_internal("`node_data/x", 12, &dir, &file, &st);
    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(1, dir_calls);
    ASSERT_EQ(0, strcmp("x", dir_name_seen[0]));
    ASSERT_EQ(1, uid_eq(&dir_uid_seen[0], NODE_DATA_UID.high, NODE_DATA_UID.low));
    ASSERT_EQ(1, uid_eq(&file, 0x5555, 5));

    /* "`node_dataX" (len 11, byte 10 not '/'): cursor stays 11 -> "X" */
    reset();
    validate_type = start_path_$node_data;
    name_$resolve_internal("`node_dataX", 11, &dir, &file, &st);
    ASSERT_EQ(1, dir_calls);
    ASSERT_EQ(0, strcmp("X", dir_name_seen[0]));
}

/* 0x00E4A160-0x00E4A16C: "." components are skipped */
TEST(dot_component_skipped)
{
    uid_t dir, file;
    status_$t st = 0;

    reset();
    validate_type = start_path_$absolute;
    add_answer("a", 1, 0xAAAA, 1, status_$ok);
    name_$resolve_internal("/./a/.", 6, &dir, &file, &st);
    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(1, dir_calls);
    ASSERT_EQ(1, uid_eq(&file, 0xAAAA, 1));
}

/* 0x00E4A178-0x00E4A18C: ".." is rejected, after dir_uid_ret is set */
TEST(dotdot_is_invalid)
{
    uid_t dir, file;
    status_$t st = 0;

    reset();
    validate_type = start_path_$absolute;
    name_$resolve_internal("/..", 3, &dir, &file, &st);
    ASSERT_EQ(status_$naming_invalid_pathname, st);
    ASSERT_EQ(0, dir_calls);
    ASSERT_EQ(1, uid_eq(&dir, NODE_UID.high, NODE_UID.low));
    ASSERT_EQ(1, uid_eq(&file, 0, 0));
}

/* 0x00E4A1C2: type 0 -> name not found; 0x00E4A1D2: type 3 -> invalid;
 * other types keep the current UID (0x00E4A1BE). */
TEST(entry_types)
{
    uid_t dir, file;
    status_$t st = 0;

    reset();
    validate_type = start_path_$absolute;
    name_$resolve_internal("/missing", 8, &dir, &file, &st);
    ASSERT_EQ(status_$naming_name_not_found, st);

    reset();
    validate_type = start_path_$absolute;
    add_answer("bad", 3, 0, 0, status_$ok);
    name_$resolve_internal("/bad", 4, &dir, &file, &st);
    ASSERT_EQ(status_$naming_invalid_pathname, st);

    reset();
    validate_type = start_path_$absolute;
    add_answer("odd", 2, 0x9999, 9, status_$ok);
    add_answer("a", 1, 0xAAAA, 1, status_$ok);
    name_$resolve_internal("/odd/a", 6, &dir, &file, &st);
    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(2, dir_calls);
    /* "a" was looked up in the NODE dir, not in 0x9999 */
    ASSERT_EQ(1, uid_eq(&dir_uid_seen[1], NODE_UID.high, NODE_UID.low));
    ASSERT_EQ(1, uid_eq(&file, 0xAAAA, 1));
}

/* 0x00E4A1A8: a DIR status is returned untouched */
TEST(dir_error_propagates)
{
    uid_t dir, file;
    status_$t st = 0;

    reset();
    validate_type = start_path_$absolute;
    add_answer("a", 1, 0xAAAA, 1, 0x000E0020);
    name_$resolve_internal("/a/b", 4, &dir, &file, &st);
    ASSERT_EQ(0x000E0020, st);
    ASSERT_EQ(1, dir_calls);
    ASSERT_EQ(1, uid_eq(&file, 0, 0));
}

/* ------------------------------------------------------------------ */
/* NAME_$RESOLVE                                                        */
/* ------------------------------------------------------------------ */

TEST(resolve_success_and_status_mapping)
{
    uid_t out;
    int16_t len = 2;
    status_$t st = 0;

    reset();
    validate_type = start_path_$absolute;
    add_answer("a", 1, 0xAAAA, 1, status_$ok);
    NAME_$RESOLVE("/a", &len, &out, &st);
    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(1, uid_eq(&out, 0xAAAA, 1));
    ASSERT_EQ(2, validate_len_seen);

    /* 0x00E4A292: 0xE0020 -> 0xE0007, and the output stays NIL */
    reset();
    validate_type = start_path_$absolute;
    add_answer("a", 1, 0xAAAA, 1, 0x000E0020);
    out.high = 0xDEAD; out.low = 0xBEEF;
    NAME_$RESOLVE("/a", &len, &out, &st);
    ASSERT_EQ(status_$naming_name_not_found, st);
    ASSERT_EQ(1, uid_eq(&out, 0, 0));
}

int main(void)
{
    printf("name_$parse_component / name_$resolve_internal / NAME_$RESOLVE tests\n");
    RUN_TEST(parse_skips_slashes_and_counts_component);
    RUN_TEST(parse_end_of_path);
    RUN_TEST(absolute_walks_from_node_dir);
    RUN_TEST(relative_is_invalid);
    RUN_TEST(network_is_invalid);
    RUN_TEST(node_data_cursor_11_or_12);
    RUN_TEST(dot_component_skipped);
    RUN_TEST(dotdot_is_invalid);
    RUN_TEST(entry_types);
    RUN_TEST(dir_error_propagates);
    RUN_TEST(resolve_success_and_status_mapping);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
