/*
 * misc/test/test_mem_lites.c - MEM_LITES (0x00E0C39C): the two light words
 * drawn at the two rows after every wait, and the exit when LITES_LOC
 * goes to zero.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    int _before = tests_failed; \
    printf("  Running %s... ", #name); \
    fflush(stdout); \
    test_##name(); \
    if (tests_failed == _before) { tests_passed++; printf("PASSED\n"); } \
} while (0)

#define ASSERT_EQ(expected, actual) do { \
    unsigned long _e = (unsigned long)(expected); \
    unsigned long _a = (unsigned long)(actual); \
    if (_e != _a) { \
        printf("FAILED\n    Expected: 0x%lx, Got: 0x%lx at line %d\n", \
               _e, _a, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

#include "misc/misc_internal.h"
#include "smd/smd.h"
#include "proc1/proc1.h"
#include "time/time.h"

int32_t LITES_LOC;
uint16_t mem_lites_row2, mem_lites_row1;
uint16_t PROC1_$CURRENT;

static uint16_t lights[2] = { 0xA5A5, 0x0F0F };
static int n_wait, stop_after, n_lites, n_clr, n_unbind, set_type, lock_set;
static uint16_t lit_pat[8], lit_y[8];
static uint32_t w_high; static uint16_t w_low, w_type;

void PROC1_$SET_TYPE(uint16_t pid, uint16_t type) { (void)pid; set_type = type; }
void PROC1_$SET_LOCK(uint16_t lock_id) { lock_set = lock_id; }
void PROC1_$CLR_LOCK(uint16_t lock_id) { if (lock_id == 0x1A) n_clr++; }
void PROC1_$UNBIND(uint16_t pid, status_$t *st) { (void)pid; n_unbind++; *st = 0; }
void TIME_$WAIT(uint16_t *delay_type, clock_t *delay, status_$t *status)
{
    w_type = *delay_type; w_high = delay->high; w_low = delay->low;
    if (++n_wait > stop_after) LITES_LOC = 0;
    *status = 0;
}
void SMD_$LITES(uint16_t pattern, uint16_t y_pos)
{
    lit_pat[n_lites] = pattern; lit_y[n_lites] = y_pos; n_lites++;
}

#include "../mem_lites.c"

TEST(two_rounds)
{
    ARCH_HOST_VA_BASE = (uintptr_t)lights - 0x1000;   /* VA 0x1000 = lights */
    LITES_LOC = 0x1000;
    mem_lites_row1 = 0x2F0; mem_lites_row2 = 0x310;
    stop_after = 2;
    MEM_LITES();
    ASSERT_EQ(10, set_type);
    ASSERT_EQ(0x1A, lock_set);
    ASSERT_EQ(3, n_wait);
    ASSERT_EQ(0, w_type);
    ASSERT_EQ(0, w_high);
    ASSERT_EQ(0x7A12, w_low);
    ASSERT_EQ(4, n_lites);
    ASSERT_EQ(0xA5A5, lit_pat[0]); ASSERT_EQ(0x2F0, lit_y[0]);
    ASSERT_EQ(0x0F0F, lit_pat[1]); ASSERT_EQ(0x310, lit_y[1]);
    ASSERT_EQ(1, n_clr);
    ASSERT_EQ(1, n_unbind);
}

int main(void)
{
    printf("MEM_LITES tests:\n");
    RUN_TEST(two_rounds);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
