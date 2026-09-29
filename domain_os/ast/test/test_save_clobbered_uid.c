/*
 * ast/test/test_save_clobbered_uid.c - Unit tests for AST_$SAVE_CLOBBERED_UID
 * (0x00E07220) and the DXM callback cell it hands to DXM_$ADD_CALLBACK.
 *
 * ast/save_clobbered_uid.c and ast/ast_data.c are #included below and the
 * real functions are called; only DXM_$ADD_CALLBACK is mocked.
 *
 * Facts under test (bead source-f4qo):
 *   - the third argument is the ADDRESS of the cell at 0x00E07272, not a
 *     function pointer: 0x00E0725A "pea (0x16,PC)" targets
 *     0x00E0725A + 2 + 0x16 = 0x00E07272
 *   - the cell holds 0x00E071EA = AST_$SET_TROUBLE (image bytes at
 *     0x00E07272 are 00 e0 71 ea), so dxm_$callback_fn() of the cell must
 *     come back as AST_$SET_TROUBLE
 *   - the data argument is a cell holding &AST_$DATA.clobbered_uid
 *     (0x00E0724E "lea (0x490,A5),A0" / 0x00E07252 "move.l A0,(-0x10,A6)" /
 *     0x00E07256 "pea (-0x10,A6)")
 *   - data_size is the word 8 (0x00E0724A "move.w #0x8,-(SP)") and check_dup
 *     is the Domain boolean true (0x00E07248 "st -(SP)")
 *   - the caller's UID is copied into AST_$DATA.clobbered_uid before the call
 *     (0x00E07230-0x00E07240)
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "ast/ast_internal.h"

/* ------------------------------------------------------------------ */
/* Test harness                                                        */
/* ------------------------------------------------------------------ */

static int tests_passed = 0;
static int tests_failed = 0;
static int current_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name)                                                        \
    do {                                                                      \
        printf("  %-44s", #name);                                             \
        current_failed = 0;                                                   \
        test_##name();                                                        \
        if (current_failed) {                                                 \
            tests_failed++;                                                   \
        } else {                                                              \
            tests_passed++;                                                   \
            printf("PASSED\n");                                               \
        }                                                                     \
    } while (0)

#define CHECK_EQ(expected, actual)                                            \
    do {                                                                      \
        long long _e = (long long)(expected);                                 \
        long long _a = (long long)(actual);                                   \
        if (_e != _a) {                                                       \
            if (!current_failed) printf("FAILED\n");                          \
            current_failed = 1;                                               \
            printf("      %s:%d: %s: expected 0x%llx, got 0x%llx\n",          \
                   __FILE__, __LINE__, #actual,                               \
                   (unsigned long long)_e, (unsigned long long)_a);           \
        }                                                                     \
    } while (0)

/* ------------------------------------------------------------------ */
/* Globals the module under test refers to                             */
/* ------------------------------------------------------------------ */

/* The AST_ module blocks (ast/ast.h). */
MODULE_DATA_DEFINE(ast_$data_t, AST_$DATA, 0x00E1DC80);
MODULE_DATA_DEFINE(ast_$aot_t, AST_$AOT, 0x00EC5400);
dxm_queue_t DXM_$UNWIRED_Q;
dxm_queue_t DXM_$WIRED_Q;
uint32_t DXM_$OVERRUNS;

/*
 * Host-side callback-cell registry.  dxm/dxm_data.c owns the real one, but
 * pulling that translation unit in would drag DXM_$ADD_SIGNAL_CALLBACK and
 * the PROC2 signal routines into this test, so the two entry points are
 * reimplemented here with the same contract (see dxm/dxm.h): a cell is a
 * 1-based handle, 0 means "unregistered".
 */
static dxm_$callback_fn_t host_callbacks[DXM_HOST_CALLBACK_MAX];
static uint32_t host_callback_count;

dxm_$callback_t dxm_$callback_cell(dxm_$callback_fn_t fn)
{
    uint32_t i;

    for (i = 0; i < host_callback_count; i++) {
        if (host_callbacks[i] == fn) {
            return (dxm_$callback_t)(i + 1);
        }
    }
    if (host_callback_count >= DXM_HOST_CALLBACK_MAX) {
        return 0;
    }
    host_callbacks[host_callback_count] = fn;
    host_callback_count++;
    return (dxm_$callback_t)host_callback_count;
}

dxm_$callback_fn_t dxm_$callback_fn(dxm_$callback_t cell)
{
    if (cell == 0 || cell > host_callback_count) {
        return NULL;
    }
    return host_callbacks[cell - 1];
}

/* ------------------------------------------------------------------ */
/* Mocks                                                               */
/* ------------------------------------------------------------------ */

static int n_add_callback;
static dxm_queue_t *last_queue;
static dxm_$callback_t last_cell_value;
static const dxm_$callback_t *last_cell_addr;
static void *last_data_value;
static uint16_t last_data_size;
static boolean last_check_dup;

void DXM_$ADD_CALLBACK(dxm_queue_t *queue, const dxm_$callback_t *callback,
                       void **data, uint16_t data_size,
                       boolean check_dup, status_$t *status_ret)
{
    n_add_callback++;
    last_queue = queue;
    last_cell_addr = callback;
    last_cell_value = *callback;
    last_data_value = *data;
    last_data_size = data_size;
    last_check_dup = check_dup;
    *status_ret = 0;
}

static int n_set_attribute;
static uid_t *last_attr_uid;
static uint16_t last_attr_id;
static uint8_t last_attr_first_byte;

void AST_$SET_ATTRIBUTE(uid_t *uid, uint16_t attr_id, void *value,
                        status_$t *status_ret)
{
    n_set_attribute++;
    last_attr_uid = uid;
    last_attr_id = attr_id;
    last_attr_first_byte = *(uint8_t *)value;
    *status_ret = 0;
}

/* ------------------------------------------------------------------ */
/* Modules under test                                                  */
/* ------------------------------------------------------------------ */

#include "../set_trouble.c"
#include "../ast_data.c"
#include "../save_clobbered_uid.c"

/* ------------------------------------------------------------------ */
/* Tests                                                               */
/* ------------------------------------------------------------------ */

static void setup(void)
{
    memset(&AST_$DATA.clobbered_uid, 0, sizeof(AST_$DATA.clobbered_uid));
    n_add_callback = 0;
    n_set_attribute = 0;
    last_queue = NULL;
    last_cell_addr = NULL;
    last_cell_value = 0;
    last_data_value = NULL;
    last_data_size = 0;
    last_check_dup = 0;
}

TEST(cell_resolves_to_set_trouble)
{
    /* The image byte pattern at 0x00E07272 is 00 e0 71 ea = AST_$SET_TROUBLE
     * at 0x00E071EA. */
    setup();
    CHECK_EQ((uintptr_t)(void *)AST_$SET_TROUBLE,
             (uintptr_t)(void *)dxm_$callback_fn(PTR_AST_$SET_TROUBLE_00e07272));
}

TEST(uid_is_copied_to_the_global)
{
    uid_t uid;

    setup();
    uid.high = 0x11223344u;
    uid.low = 0x55667788u;

    AST_$SAVE_CLOBBERED_UID(&uid);

    CHECK_EQ(0x11223344u, AST_$DATA.clobbered_uid.high);
    CHECK_EQ(0x55667788u, AST_$DATA.clobbered_uid.low);
}

TEST(callback_is_queued_on_the_unwired_queue)
{
    uid_t uid;

    setup();
    uid.high = 1;
    uid.low = 2;

    AST_$SAVE_CLOBBERED_UID(&uid);

    CHECK_EQ(1, n_add_callback);
    /* 0x00E0725E move.l #0xe2adc4,-(SP) = &DXM_$UNWIRED_Q */
    CHECK_EQ((uintptr_t)&DXM_$UNWIRED_Q, (uintptr_t)last_queue);
    /* 0x00E0725A pea (0x16,PC) -> the ADDRESS of the cell */
    CHECK_EQ((uintptr_t)&PTR_AST_$SET_TROUBLE_00e07272,
             (uintptr_t)last_cell_addr);
    CHECK_EQ(PTR_AST_$SET_TROUBLE_00e07272, last_cell_value);
    /* 0x00E0724E-0x00E07256: the datum is a cell holding the global's
     * address, not the UID bytes themselves. */
    CHECK_EQ((uintptr_t)&AST_$DATA.clobbered_uid, (uintptr_t)last_data_value);
    /* 0x00E0724A move.w #0x8 / 0x00E07248 st */
    CHECK_EQ(8, last_data_size);
    CHECK_EQ((int8_t)0xFF, last_check_dup);
}

TEST(queued_callback_runs_set_trouble_on_the_saved_uid)
{
    uid_t uid;
    void *datum;

    setup();
    uid.high = 0xdeadbeefu;
    uid.low = 0xcafebabeu;

    AST_$SAVE_CLOBBERED_UID(&uid);

    /* DXM copies `data_size` bytes of the datum into the queue entry and
     * later calls the cell's function with the entry's address, so the
     * callback sees a uid_t ** just like AST_$SET_TROUBLE expects. */
    datum = last_data_value;
    dxm_$callback_fn(last_cell_value)(&datum);

    CHECK_EQ(1, n_set_attribute);
    CHECK_EQ((uintptr_t)&AST_$DATA.clobbered_uid, (uintptr_t)last_attr_uid);
    CHECK_EQ(2, last_attr_id);
    CHECK_EQ(0xFF, last_attr_first_byte);
    CHECK_EQ(0xdeadbeefu, last_attr_uid->high);
    CHECK_EQ(0xcafebabeu, last_attr_uid->low);
}

int main(void)
{
    printf("test_save_clobbered_uid:\n");

    RUN_TEST(cell_resolves_to_set_trouble);
    RUN_TEST(uid_is_copied_to_the_global);
    RUN_TEST(callback_is_queued_on_the_unwired_queue);
    RUN_TEST(queued_callback_runs_set_trouble_on_the_saved_uid);

    printf("\n  Results: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
