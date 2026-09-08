/*
 * audit/test/test_load_list.c - unit tests for audit_$load_list (0x00E7131C).
 *
 * audit/load_list.c is #included below with NAME_$RESOLVE, FILE_$LOCK /
 * FILE_$UNLOCK, MST_$MAPS / MST_$UNMAP_PRIVI, the ML exclusion pair, the two
 * hash helpers and EC_$ADVANCE mocked.  The tests pin down bead source-npje:
 * the backquote path and its 27-byte length cell, and the FILE_$LOCK /
 * FILE_$UNLOCK constant cells that the C had replaced with zero locals.
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
#include "mst/mst.h"

/* ------------------------------------------------------------------ */

audit_data_t AUDIT_$DATA;
uid_t        UID_$NIL = { 0, 0 };
uint16_t     PROC1_$AS_ID;

/* ------------------------------------------------------------------ */
/* Mocks                                                               */
/* ------------------------------------------------------------------ */

static const char *resolve_path;
static int16_t     resolve_len;
static status_$t   resolve_result;

void NAME_$RESOLVE(char *path, int16_t *path_len, uid_t *resolved_uid,
                   status_$t *status_ret)
{
    resolve_path = path;
    resolve_len  = *path_len;
    resolved_uid->high = 0xAAAA0000u;
    resolved_uid->low  = 0x0000BBBBu;
    *status_ret = resolve_result;
}

static int             lock_calls;
static const uint16_t *lock_index_arg;
static const uint16_t *lock_mode_arg;
static const uint8_t  *lock_rights_arg;
static const void     *lock_info_arg;
static status_$t       lock_result;

void FILE_$LOCK(uid_t *file_uid, const uint16_t *lock_index,
                const uint16_t *lock_mode, const uint8_t *rights,
                void *lock_info, status_$t *status_ret)
{
    lock_calls++;
    lock_index_arg  = lock_index;
    lock_mode_arg   = lock_mode;
    lock_rights_arg = rights;
    lock_info_arg   = lock_info;
    (void)file_uid;
    *status_ret = lock_result;
}

static int             unlock_calls;
static const uint16_t *unlock_mode_arg;

void FILE_$UNLOCK(uid_t *file_uid, uint16_t *lock_mode, status_$t *status_ret)
{
    unlock_calls++;
    unlock_mode_arg = lock_mode;
    (void)file_uid;
    (void)status_ret;   /* 0x00E71482 leaves the caller's status alone */
}

static uint8_t   list_image[0x40];
static int       maps_calls;
static status_$t maps_result;

void *MST_$MAPS(int16_t asid, boolean direction, uid_t *uid, uint32_t start_va,
                uint32_t length, int16_t area_id, uint32_t area_size,
                boolean access_rights, void *map_info, status_$t *status)
{
    maps_calls++;
    (void)asid; (void)direction; (void)uid; (void)start_va; (void)length;
    (void)area_id; (void)area_size; (void)access_rights;
    *(uint32_t *)map_info = sizeof(list_image);
    *status = maps_result;
    return list_image;
}

static int unmap_calls;
void MST_$UNMAP_PRIVI(int16_t flags, uid_t *uid, uint32_t va, uint32_t len,
                      uint16_t asid, status_$t *status)
{
    unmap_calls++;
    (void)flags; (void)uid; (void)va; (void)len; (void)asid; (void)status;
}

static int excl_start_calls, excl_stop_calls;
void ML_$EXCLUSION_START(ml_$exclusion_t *e) { excl_start_calls++; (void)e; }
void ML_$EXCLUSION_STOP(ml_$exclusion_t *e)  { excl_stop_calls++;  (void)e; }

static int clear_calls;
static status_$t *clear_status_arg;
static status_$t *load_status_cell;
void audit_$clear_hash_table(status_$t *status_ret)
{
    clear_calls++;
    clear_status_arg = status_ret;
}

static int   add_calls;
static uid_t add_uids[8];
void audit_$add_to_hash(uid_t *uid, status_$t *status_ret)
{
    if (add_calls < 8) {
        add_uids[add_calls] = *uid;
    }
    add_calls++;
    *status_ret = status_$ok;
}

static int advance_calls;
void EC_$ADVANCE(ec_$eventcount_t *ec) { advance_calls++; (void)ec; }

#include "../load_list.c"

/* ------------------------------------------------------------------ */

static uint8_t wired[64];

static int8_t run_load(status_$t resolve_st, uint16_t version,
                       uint16_t entries, status_$t *status_out)
{
    audit_list_header_t *hdr = (audit_list_header_t *)list_image;
    uid_t *uids;
    unsigned i;
    status_$t status = 0x7F7F7F7F;
    int8_t r;

    memset(&AUDIT_$DATA, 0, sizeof(AUDIT_$DATA));
    AUDIT_$DATA.event_count = (ec_$eventcount_t *)wired;
    memset(list_image, 0, sizeof(list_image));

    hdr->list_uid.high = 0x01020304u;
    hdr->list_uid.low  = 0x05060708u;
    hdr->timeout_units = 5;
    hdr->version       = version;
    hdr->entry_count   = entries;
    hdr->flags         = 0x0003;

    uids = (uid_t *)(void *)(list_image + 0x10);
    for (i = 0; i < 3; i++) {
        uids[i].high = 0x1000u + i;
        uids[i].low  = 0x2000u + i;
    }

    resolve_result = resolve_st;
    lock_result = status_$ok;
    maps_result = status_$ok;
    resolve_path = NULL;
    resolve_len = -1;
    lock_calls = unlock_calls = maps_calls = unmap_calls = 0;
    lock_index_arg = lock_mode_arg = NULL;
    lock_rights_arg = NULL;
    lock_info_arg = NULL;
    unlock_mode_arg = NULL;
    excl_start_calls = excl_stop_calls = 0;
    clear_calls = add_calls = advance_calls = 0;
    clear_status_arg = NULL;
    memset(add_uids, 0, sizeof(add_uids));

    load_status_cell = &status;
    r = audit_$load_list(&status);
    if (status_out != NULL) {
        *status_out = status;
    }
    return r;
}

/*
 * 0x00E7149A: 60 6e 6f 64 65 5f 64 61 74 61 ... - backquote, then
 * "node_data/audit/audit_list".  0x00E71494 holds 0x001B = 27.
 */
TEST(path_cell)
{
    ASSERT_EQ('`', list_path[0]);
    ASSERT_EQ(0, strcmp(list_path, "`node_data/audit/audit_list"));
    ASSERT_EQ(27, sizeof(list_path) - 1);
    ASSERT_EQ(0x001B, list_path_len);
}

/* 0x00E71496 = 0x0001 and 0x00E71498 = 0x0000. */
TEST(lock_cells)
{
    ASSERT_EQ(0x0001, list_lock_one);
    ASSERT_EQ(0x0000, list_lock_zero);
}

TEST(resolve_uses_the_image_path)
{
    run_load(status_$ok, 1, 3, NULL);
    ASSERT_TRUE(resolve_path == list_path);
    ASSERT_EQ(0x001B, resolve_len);
}

/*
 * 0x00E7135E-0x00E71366.  lock_index and lock_mode are literally the SAME
 * cell (value 1); rights is the separate zero cell; lock_info is a frame
 * local, not a null pointer.
 */
TEST(file_lock_arguments)
{
    run_load(status_$ok, 1, 3, NULL);

    ASSERT_EQ(1, lock_calls);
    ASSERT_TRUE(lock_index_arg == (const uint16_t *)&list_lock_one);
    ASSERT_TRUE(lock_mode_arg == lock_index_arg);
    ASSERT_EQ(1, *lock_index_arg);
    ASSERT_TRUE(lock_rights_arg == (const uint8_t *)&list_lock_zero);
    ASSERT_TRUE(lock_info_arg != NULL);
}

/* 0x00E7147A: FILE_$UNLOCK's mode is the same 0x00E71496 cell = 1. */
TEST(file_unlock_uses_the_same_cell)
{
    run_load(status_$ok, 1, 3, NULL);

    ASSERT_EQ(1, unlock_calls);
    ASSERT_TRUE(unlock_mode_arg == (const uint16_t *)&list_lock_one);
    ASSERT_EQ(1, *unlock_mode_arg);
}

/* 0x00E7134A-0x00E7134E: name-not-found is not an error. */
TEST(missing_list_is_not_an_error)
{
    status_$t status;
    int8_t r = run_load(status_$naming_name_not_found, 1, 0, &status);

    ASSERT_EQ(0, r);
    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(0, lock_calls);
    ASSERT_EQ(0, unlock_calls);
}

/* 0x00E713EE-0x00E71434: the header fields and the entry walk. */
TEST(loads_the_header_and_entries)
{
    status_$t status;
    int8_t r = run_load(status_$ok, 1, 3, &status);

    ASSERT_EQ((int8_t)-1, r);            /* 0x00E71454: st D2b */
    ASSERT_EQ(1, clear_calls);
    /*
     * source-fxlx: the nested procedure forwards audit_$load_list's OWN
     * status_ret (its A6+0x08, read through A2 at 0x00E71294), so the
     * pointer it hands audit_$alloc must be the very cell the caller
     * passed in.
     */
    ASSERT_TRUE(clear_status_arg == load_status_cell);
    ASSERT_EQ(0x0003, AUDIT_$DATA.flags);
    ASSERT_EQ(0x01020304u, AUDIT_$DATA.list_uid.high);
    ASSERT_EQ(0x05060708u, AUDIT_$DATA.list_uid.low);
    ASSERT_EQ(3, AUDIT_$DATA.list_count);
    ASSERT_EQ(20, AUDIT_$DATA.timeout);  /* 0x00E71404: `lsl.w #0x2` */
    ASSERT_EQ(3, add_calls);
    ASSERT_EQ(0x1000u, add_uids[0].high);
    ASSERT_EQ(0x1002u, add_uids[2].high);
    ASSERT_EQ(1, advance_calls);
    ASSERT_EQ(1, unmap_calls);
    ASSERT_EQ(1, unlock_calls);
}

/* 0x00E713B8: `cmpi.w #0x1` / `ble` - version 2 is refused. */
TEST(version_check)
{
    status_$t status;
    int8_t r = run_load(status_$ok, 2, 1, &status);

    ASSERT_EQ(0, r);
    ASSERT_EQ(0x00300010, status);
    ASSERT_EQ(0, clear_calls);
    ASSERT_EQ(1, unmap_calls);   /* the failure still unmaps and unlocks */
    ASSERT_EQ(1, unlock_calls);
}

/* 0x00E713CA: `cmpi.w #0x100` / `ble`. */
TEST(entry_count_check)
{
    status_$t status;
    int8_t r = run_load(status_$ok, 1, 0x101, &status);

    ASSERT_EQ(0, r);
    ASSERT_EQ(0x00300003, status);
    ASSERT_EQ(0, clear_calls);
}

int main(void)
{
    printf("audit_$load_list tests\n");

    RUN_TEST(path_cell);
    RUN_TEST(lock_cells);
    RUN_TEST(resolve_uses_the_image_path);
    RUN_TEST(file_lock_arguments);
    RUN_TEST(file_unlock_uses_the_same_cell);
    RUN_TEST(missing_list_is_not_an_error);
    RUN_TEST(loads_the_header_and_entries);
    RUN_TEST(version_check);
    RUN_TEST(entry_count_check);

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
