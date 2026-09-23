/*
 * peb/test/test_regs.c - PEB_$LOAD_REGS / PEB_$UNLOAD_REGS / PEB_$GET_FP /
 * PEB_$PUT_FP (0x00E5AE60..0x00E5AF36)
 *
 * The board page is a host arena; the tests pin the two asymmetric
 * register-offset lists and the asid*0x1C record selection.
 */

#include <stdio.h>
#include <string.h>

int __host_intr_disable_count = 0;

#include "peb/peb_internal.h"

peb_globals_t   peb_globals;
peb_fp_state_t  PEB_$WIRED_DATA_START[PEB_MAX_PROCESSES];
static uint8_t  host_reg_page[0x400];
volatile uint8_t *peb_reg_page_7000 = host_reg_page;

#include "../regs.c"

static int tests_passed = 0;
static int tests_failed = 0;
#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { printf("  Running %-44s ", #name); test_##name(); \
    tests_passed++; printf("PASSED\n"); } while (0)
#define ASSERT_EQ(expected, actual) do { \
    unsigned long long _e = (unsigned long long)(expected); \
    unsigned long long _a = (unsigned long long)(actual); \
    if (_e != _a) { printf("FAILED\n    Expected: 0x%llx, Got: 0x%llx at line %d\n", \
        _e, _a, __LINE__); tests_failed++; return; } } while (0)

static uint32_t reg(uint32_t off) { uint32_t v; memcpy(&v, host_reg_page + off, 4); return v; }
static void set_reg(uint32_t off, uint32_t v) { memcpy(host_reg_page + off, &v, 4); }

TEST(load_writes_seven_input_registers)
{
    peb_fp_state_t s = { { 0x11111111, 0x22222222, 0x33333333, 0x44444444 },
                         0x55555555, 0x66666666, 0x77777777 };
    memset(host_reg_page, 0, sizeof host_reg_page);
    PEB_$LOAD_REGS(&s);
    ASSERT_EQ(0x11111111, reg(0x94));
    ASSERT_EQ(0x22222222, reg(0x98));
    ASSERT_EQ(0x33333333, reg(0x1B0));
    ASSERT_EQ(0x44444444, reg(0x1B4));
    ASSERT_EQ(0x55555555, reg(0xF4));
    ASSERT_EQ(0x66666666, reg(0x84));
    ASSERT_EQ(0x77777777, reg(0x104));
    ASSERT_EQ(0, reg(0x8C));                 /* output registers untouched */
    ASSERT_EQ(0, reg(0x1DC));
}

TEST(unload_reads_seven_output_registers)
{
    peb_fp_state_t s;
    memset(host_reg_page, 0, sizeof host_reg_page);
    set_reg(0x8C, 0xA1); set_reg(0x90, 0xA2); set_reg(0x1D0, 0xA3); set_reg(0x1D4, 0xA4);
    set_reg(0xF4, 0xA5); set_reg(0x1DC, 0xA6); set_reg(0x104, 0xA7);
    set_reg(0x94, 0xBAD); set_reg(0x84, 0xBAD);
    PEB_$UNLOAD_REGS(&s);
    ASSERT_EQ(0xA1, s.data_regs[0]);
    ASSERT_EQ(0xA2, s.data_regs[1]);
    ASSERT_EQ(0xA3, s.data_regs[2]);
    ASSERT_EQ(0xA4, s.data_regs[3]);
    ASSERT_EQ(0xA5, s.status_reg);
    ASSERT_EQ(0xA6, s.ctrl_reg);
    ASSERT_EQ(0xA7, s.instr_counter);
}

TEST(get_fp_selects_record_by_asid)
{
    int16_t asid = 5;
    memset(host_reg_page, 0, sizeof host_reg_page);
    memset(PEB_$WIRED_DATA_START, 0, sizeof PEB_$WIRED_DATA_START);
    PEB_$WIRED_DATA_START[5].data_regs[0] = 0xC0FFEE;
    PEB_$WIRED_DATA_START[5].instr_counter = 0xFACE;
    PEB_$GET_FP(&asid);
    ASSERT_EQ(0xC0FFEE, reg(0x94));
    ASSERT_EQ(0xFACE, reg(0x104));
}

TEST(put_fp_stores_record_by_asid)
{
    int16_t asid = 7;
    memset(host_reg_page, 0, sizeof host_reg_page);
    memset(PEB_$WIRED_DATA_START, 0, sizeof PEB_$WIRED_DATA_START);
    set_reg(0x8C, 0xD00D); set_reg(0x1DC, 0xBEEF);
    PEB_$PUT_FP(&asid);
    ASSERT_EQ(0xD00D, PEB_$WIRED_DATA_START[7].data_regs[0]);
    ASSERT_EQ(0xBEEF, PEB_$WIRED_DATA_START[7].ctrl_reg);
    ASSERT_EQ(0, PEB_$WIRED_DATA_START[6].data_regs[0]);
    ASSERT_EQ(0, PEB_$WIRED_DATA_START[8].data_regs[0]);
}

int main(void)
{
    printf("PEB register tests\n");
    RUN_TEST(load_writes_seven_input_registers);
    RUN_TEST(unload_reads_seven_output_registers);
    RUN_TEST(get_fp_selects_record_by_asid);
    RUN_TEST(put_fp_stores_record_by_asid);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
