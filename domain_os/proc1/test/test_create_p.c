/*
 * Tests for PROC1_$CREATE_P (0x00E15148).
 *
 * Includes the real proc1/create_p.c and drives it through mocked
 * PROC1_$ALLOC_STACK / PROC1_$BIND / PROC1_$FREE_STACK / PROC1_$RESUME.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "base/base.h"
#include "proc1/proc1.h"

int __host_intr_disable_count = 0;

/* ------------------------------------------------------------------ */
/* Module cells                                                        */
/* ------------------------------------------------------------------ */

uint16_t PROC1_$TYPE[PROC1_MAX_PROCESSES];

/* ------------------------------------------------------------------ */
/* Mocks                                                               */
/* ------------------------------------------------------------------ */

static int n_alloc;
static uint16_t alloc_size;
static status_$t alloc_status;
static uint8_t alloc_stack_cell;

void *PROC1_$ALLOC_STACK(uint16_t size, status_$t *status_ret)
{
    n_alloc++;
    alloc_size = size;
    *status_ret = alloc_status;
    return &alloc_stack_cell;
}

static int n_bind;
static void *bind_entry, *bind_sp, *bind_base;
static uint16_t bind_ws_param;
static status_$t bind_status;
static uint16_t bind_pid;

uint16_t PROC1_$BIND(void *entry, void *initial_sp, void *stack_base,
                     uint16_t ws_param, status_$t *status_p)
{
    n_bind++;
    bind_entry = entry;
    bind_sp = initial_sp;
    bind_base = stack_base;
    bind_ws_param = ws_param;
    *status_p = bind_status;
    return bind_pid;
}

static int n_free;
static void *freed_stack;

void PROC1_$FREE_STACK(void *stack) { n_free++; freed_stack = stack; }

static int n_resume;
static uint16_t resume_pid;
static status_$t resume_status;
static uint16_t type_at_resume;

void PROC1_$RESUME(uint16_t pid, status_$t *status_p)
{
    n_resume++;
    resume_pid = pid;
    type_at_resume = PROC1_$TYPE[pid];
    *status_p = resume_status;
}

/* ------------------------------------------------------------------ */
/* Code under test                                                     */
/* ------------------------------------------------------------------ */

#include "../create_p.c"

/* ------------------------------------------------------------------ */

static int tests_run, tests_failed;

#define ASSERT_EQ(a, b) do {                                                  \
    unsigned long long _a = (unsigned long long)(a);                          \
    unsigned long long _b = (unsigned long long)(b);                          \
    if (_a != _b) {                                                           \
        printf("  FAIL %s:%d: %s == %s (0x%llx != 0x%llx)\n", __FILE__,       \
               __LINE__, #a, #b, _a, _b);                                     \
        tests_failed++;                                                       \
    }                                                                         \
} while (0)

#define RUN_TEST(fn) do { tests_run++; reset(); fn(); } while (0)

static void reset(void)
{
    memset(PROC1_$TYPE, 0xEE, sizeof(PROC1_$TYPE));
    n_alloc = n_bind = n_free = n_resume = 0;
    alloc_status = status_$ok;
    bind_status = status_$ok;
    bind_pid = 7;
    resume_status = status_$ok;
}

static int entry_marker;

/*
 * 0x00E15156 / 0x00E15162: the HIGH word of the packed argument is the
 * stack size and the LOW word is the type (`move.l #0x800000f' at
 * 0x00E2F854 creates a 0x800-byte stack of type 0xF).
 */
static void test_argument_halves(void)
{
    status_$t st;
    uint16_t pid = PROC1_$CREATE_P(&entry_marker, 0x0800000Fu, &st);

    ASSERT_EQ(alloc_size, 0x0800);
    ASSERT_EQ(pid, 7);
    ASSERT_EQ(st, status_$ok);
    /* 0x00E151E2: PROC1_$TYPE[pid] = type, before the resume */
    ASSERT_EQ(PROC1_$TYPE[7], 0x000F);
    ASSERT_EQ(type_at_resume, 0x000F);
    ASSERT_EQ(n_resume, 1);
    ASSERT_EQ(resume_pid, 7);
}

/* 0x00E151B2..0x00E151C0: BIND gets entry, the stack twice, ws_param */
static void test_bind_arguments(void)
{
    status_$t st;

    (void)PROC1_$CREATE_P(&entry_marker, 0x04000003u, &st);

    ASSERT_EQ(n_bind, 1);
    ASSERT_EQ((uintptr_t)bind_entry, (uintptr_t)&entry_marker);
    ASSERT_EQ((uintptr_t)bind_sp, (uintptr_t)&alloc_stack_cell);
    ASSERT_EQ((uintptr_t)bind_base, (uintptr_t)&alloc_stack_cell);
    ASSERT_EQ(bind_ws_param, 5);
}

/* 0x00E15176..0x00E151AA: the jump table at 0x00E1518A, type by type */
static void test_ws_param_table(void)
{
    static const struct { uint16_t type; uint16_t ws; } cases[] = {
        { 0, 0 }, { 1, 0 }, { 2, 0 },
        { 3, 5 }, { 4, 5 }, { 5, 5 },
        { 6, 0 }, { 7, 0 },
        { 8, 6 },
        { 9, 0 },
        { 10, 5 },
        { 11, 0 }, { 0x000F, 0 }, { 0xFFFF, 0 },
    };
    unsigned i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        status_$t st;
        reset();
        (void)PROC1_$CREATE_P(&entry_marker, 0x00010000u | cases[i].type, &st);
        if (bind_ws_param != cases[i].ws) {
            printf("  FAIL type %u: ws_param %u, expected %u\n",
                   cases[i].type, bind_ws_param, cases[i].ws);
            tests_failed++;
        }
    }
}

/*
 * 0x00E15170 / 0x00E151F2: an ALLOC_STACK failure returns the TYPE word
 * (D2 has not become the pid) and binds nothing.
 */
static void test_alloc_failure_returns_type(void)
{
    status_$t st;
    uint16_t pid;

    alloc_status = status_$no_stack_space_is_available;
    pid = PROC1_$CREATE_P(&entry_marker, 0x1000000Au, &st);

    ASSERT_EQ(pid, 0x000A);
    ASSERT_EQ(st, status_$no_stack_space_is_available);
    ASSERT_EQ(n_bind, 0);
    ASSERT_EQ(n_free, 0);
    ASSERT_EQ(n_resume, 0);
}

/*
 * 0x00E151CA..0x00E151D8: a BIND failure frees the stack, leaves
 * PROC1_$TYPE alone and returns the type word.
 */
static void test_bind_failure_frees_stack(void)
{
    status_$t st;
    uint16_t pid;

    bind_status = status_$no_pcb_is_available;
    bind_pid = 9;
    pid = PROC1_$CREATE_P(&entry_marker, 0x10000004u, &st);

    ASSERT_EQ(pid, 0x0004);
    ASSERT_EQ(st, status_$no_pcb_is_available);
    ASSERT_EQ(n_free, 1);
    ASSERT_EQ((uintptr_t)freed_stack, (uintptr_t)&alloc_stack_cell);
    ASSERT_EQ(PROC1_$TYPE[9], 0xEEEE);
    ASSERT_EQ(n_resume, 0);
}

/*
 * 0x00E15170 / 0x00E151CA: `tst.w (0x2,A2)' looks at the low word only,
 * so a status with nothing in its low word counts as success.
 */
static void test_status_low_word_only(void)
{
    status_$t st;
    uint16_t pid;

    alloc_status = 0x00010000;
    bind_status = 0x00120000;
    pid = PROC1_$CREATE_P(&entry_marker, 0x04000008u, &st);

    ASSERT_EQ(pid, 7);
    ASSERT_EQ(n_bind, 1);
    ASSERT_EQ(n_resume, 1);
    ASSERT_EQ(bind_ws_param, 6);
}

/* 0x00E151E8: the status the caller sees is PROC1_$RESUME's */
static void test_resume_status_passed_through(void)
{
    status_$t st;

    resume_status = status_$process_not_suspended;
    (void)PROC1_$CREATE_P(&entry_marker, 0x04000003u, &st);

    ASSERT_EQ(st, status_$process_not_suspended);
}

int main(void)
{
    RUN_TEST(test_argument_halves);
    RUN_TEST(test_bind_arguments);
    RUN_TEST(test_ws_param_table);
    RUN_TEST(test_alloc_failure_returns_type);
    RUN_TEST(test_bind_failure_frees_stack);
    RUN_TEST(test_status_low_word_only);
    RUN_TEST(test_resume_status_passed_through);

    printf("test_create_p: %d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed ? 1 : 0;
}
