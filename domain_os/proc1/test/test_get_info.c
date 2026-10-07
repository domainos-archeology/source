/*
 * Tests for PROC1_$GET_INFO (0x00E14F52) and PROC1_$GET_LIST (0x00E15362).
 * Includes the real proc1/get_info.c and proc1/get_list.c with ADD48 and
 * PROC1_$GET_INFO_INT mocked.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "base/base.h"
#include "proc1/proc1.h"
#include "cal/cal.h"

int __host_intr_disable_count = 0;

static proc1_t pcb_table[PROC1_MAX_PROCESSES];
proc1_t *PCBS[PROC1_MAX_PROCESSES];
proc1_t *PROC1_$CURRENT_PCB;
MODULE_DATA_DEFINE(proc1_$data_t, PROC1_$DATA, 0x00E254E8);

static int n_add48;
static clock_t *add48_dst;
static const clock_t *add48_src;

void ADD48(clock_t *dst, clock_t *src)
{
    uint32_t lo;
    n_add48++;
    add48_dst = dst;
    add48_src = src;
    lo = (uint32_t)dst->low + src->low;
    dst->low = (uint16_t)lo;
    dst->high = dst->high + src->high + (lo >> 16);
}

static int n_info_int;
static uint16_t ii_pid;
static void *ii_base, *ii_top;
static uint16_t *ii_usr; static uint32_t *ii_upc, *ii_usb, *ii_usp;

/* The Pascal frame packs pid.w and six unpadded longwords into seven gcc
 * slots (proc1/proc1.h); unpack them the way the asm reads them. */
void (PROC1_$GET_INFO_INT)(uint32_t s1, uint32_t s2, uint32_t s3, uint32_t s4,
                           uint32_t s5, uint32_t s6, uint32_t s7)
{
    uint16_t pid = ARCH_PASCAL_SLOT_WORD(s1);
    void *stack_base = ARCH_VA_TO_PTR(ARCH_PASCAL_SLOTS_LONG(s1, s2));
    void *stack_top = ARCH_VA_TO_PTR(ARCH_PASCAL_SLOTS_LONG(s2, s3));
    uint16_t *usr_ret = ARCH_VA_TO_PTR(ARCH_PASCAL_SLOTS_LONG(s3, s4));
    uint32_t *upc_ret = ARCH_VA_TO_PTR(ARCH_PASCAL_SLOTS_LONG(s4, s5));
    uint32_t *usb_ret = ARCH_VA_TO_PTR(ARCH_PASCAL_SLOTS_LONG(s5, s6));
    uint32_t *usp_ret = ARCH_VA_TO_PTR(ARCH_PASCAL_SLOTS_LONG(s6, s7));
    n_info_int++;
    ii_pid = pid; ii_base = stack_base; ii_top = stack_top;
    ii_usr = usr_ret; ii_upc = upc_ret; ii_usb = usb_ret; ii_usp = usp_ret;
    *usr_ret = 0x2700; *upc_ret = 0x00E00001; *usb_ret = 0x00EB1000; *usp_ret = 0x00EB0F00;
}

#include "../get_info.c"
#include "../get_list.c"

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

static proc1_$info_t info;

static void reset(void)
{
    unsigned i;
    memset(pcb_table, 0, sizeof(pcb_table));
    for (i = 0; i < PROC1_MAX_PROCESSES; i++) {
        PCBS[i] = &pcb_table[i];
        pcb_table[i].mypid = (uint16_t)i;
        PROC1_$DATA.os_stack_base[i] = 0;
        PROC1_$DATA.type[i] = (uint16_t)(0x100 + i);
    }
    PROC1_$CURRENT_PCB = &pcb_table[1];
    memset(&info, 0xCC, sizeof(info));
    n_add48 = 0;
    n_info_int = 0;
}

/* 0x00E14F70..0x00E14F8E: pid 0, pid > 0x40 and a NULL slot are illegal */
static void test_info_illegal_pid(void)
{
    status_$t st;
    int16_t pid;

    pid = 0;
    PROC1_$GET_INFO(&pid, &info, &st);
    ASSERT_EQ(st, status_$illegal_process_id);

    pid = 0x41;
    PROC1_$GET_INFO(&pid, &info, &st);
    ASSERT_EQ(st, status_$illegal_process_id);

    pid = 5;
    PCBS[5] = NULL;
    PROC1_$GET_INFO(&pid, &info, &st);
    ASSERT_EQ(st, status_$illegal_process_id);

    /* 0x00E14F6E: status is cleared first; nothing else is written */
    ASSERT_EQ(info.flags, 0xCCCC);
    ASSERT_EQ(n_add48, 0);
}

/* 0x00E14F96 / 0x00E14F9E */
static void test_info_not_bound(void)
{
    status_$t st;
    int16_t pid = 0x40;

    PROC1_$GET_INFO(&pid, &info, &st);
    ASSERT_EQ(st, status_$process_not_bound);
    ASSERT_EQ(info.flags, 0xCCCC);
}

/*
 * 0x00E14FA6..0x00E14FCC: flags word, doubled CPU time, dragged state,
 * and nothing more for the current process.
 */
static void test_info_current_process(void)
{
    status_$t st;
    int16_t pid = 1;
    proc1_t *p = &pcb_table[1];

    p->pri_min = 0x12;
    p->pri_max = PROC1_FLAG_BOUND | PROC1_FLAG_WAITING;
    p->cpu_total = 0x00000100;
    p->cpu_usage = 0x8000;
    p->state = 0x0014;
    PROC1_$GET_INFO(&pid, &info, &st);

    ASSERT_EQ(st, status_$ok);
    ASSERT_EQ(info.flags, 0x1209);
    ASSERT_EQ(n_add48, 1);
    ASSERT_EQ((uintptr_t)add48_dst, (uintptr_t)&info.cpu.time);
    ASSERT_EQ((uintptr_t)add48_src, (uintptr_t)&p->cpu_total);
    ASSERT_EQ(info.cpu.time.high, 0x00000201);
    ASSERT_EQ(info.cpu.time.low, 0x0000);
    ASSERT_EQ(info.cpu.state, 0x0014);
    /* register fields untouched */
    ASSERT_EQ(info.usr, 0xCCCC);
    ASSERT_EQ(info.upc, 0xCCCCCCCC);
    ASSERT_EQ(info.usb, 0xCCCCCCCC);
    ASSERT_EQ(info.usp, 0xCCCCCCCC);
    ASSERT_EQ(n_info_int, 0);
}

/* 0x00E14FD8..0x00E14FFC: GET_INFO_INT with stack-0x1000, stack, four slots */
static void test_info_other_process_with_stack(void)
{
    static uint8_t arena[0x2000];
    status_$t st;
    int16_t pid = 7;

    /* every pointer crosses the call as a 32-bit VA (the straddled Pascal
     * longwords): base the window below both the arena and `info' */
    ARCH_HOST_VA_BASE = (uintptr_t)arena < (uintptr_t)&info
                            ? (uintptr_t)arena : (uintptr_t)&info;
    pcb_table[7].pri_max = PROC1_FLAG_BOUND;
    PROC1_$DATA.os_stack_base[7] = ARCH_PTR_TO_VA(arena + 0x1800);
    PROC1_$GET_INFO(&pid, &info, &st);
    ARCH_HOST_VA_BASE = 0;

    ASSERT_EQ(st, status_$ok);
    ASSERT_EQ(n_info_int, 1);
    ASSERT_EQ(ii_pid, 7);
    ASSERT_EQ((uintptr_t)ii_base, (uintptr_t)(arena + 0x0800));
    ASSERT_EQ((uintptr_t)ii_top, (uintptr_t)(arena + 0x1800));
    ASSERT_EQ((uintptr_t)ii_usr, (uintptr_t)&info.usr);
    ASSERT_EQ((uintptr_t)ii_upc, (uintptr_t)&info.upc);
    ASSERT_EQ((uintptr_t)ii_usb, (uintptr_t)&info.usb);
    ASSERT_EQ((uintptr_t)ii_usp, (uintptr_t)&info.usp);
    ASSERT_EQ(info.usr, 0x2700);
    ASSERT_EQ(info.usb, 0x00EB1000);
}

/* 0x00E15004..0x00E1500E: no stack -> usr/upc/usp/usb cleared, cpu kept */
static void test_info_other_process_no_stack(void)
{
    status_$t st;
    int16_t pid = 7;

    pcb_table[7].pri_max = PROC1_FLAG_BOUND;
    pcb_table[7].state = 0x0011;
    PROC1_$GET_INFO(&pid, &info, &st);

    ASSERT_EQ(st, status_$ok);
    ASSERT_EQ(n_info_int, 0);
    ASSERT_EQ(info.usr, 0);
    ASSERT_EQ(info.upc, 0);
    ASSERT_EQ(info.usp, 0);
    ASSERT_EQ(info.usb, 0);
    ASSERT_EQ(info.cpu.state, 0x0011);
}

/* 0x00E15362: bound, asid 0, slots 0..0x40, NULL slots skipped */
static void test_list_selection(void)
{
    int16_t count = 77;
    proc_list_entry_t list[8];

    memset(list, 0xEE, sizeof(list));
    pcb_table[0].pri_max = PROC1_FLAG_BOUND;                /* slot 0 counts */
    pcb_table[3].pri_max = PROC1_FLAG_BOUND;
    pcb_table[3].asid = 5;                                  /* excluded */
    pcb_table[4].pri_max = PROC1_FLAG_SUSPENDED;            /* not bound */
    pcb_table[9].pri_max = PROC1_FLAG_BOUND | PROC1_FLAG_SUSPENDED;
    pcb_table[9].mypid = 0x22;                              /* type by mypid */
    PCBS[10] = NULL;
    pcb_table[0x40].pri_max = PROC1_FLAG_BOUND;

    PROC1_$GET_LIST(&count, list);

    ASSERT_EQ(count, 3);
    ASSERT_EQ(list[0].pid, 0);
    ASSERT_EQ(list[0].type, 0x100);
    ASSERT_EQ(list[1].pid, 0x22);
    ASSERT_EQ(list[1].type, 0x122);
    ASSERT_EQ(list[2].pid, 0x40);
    ASSERT_EQ(list[2].type, 0x140);
    ASSERT_EQ(list[3].pid, 0xEEEE);
}

/* 0x00E1537A: an empty result still zeroes the count */
static void test_list_empty(void)
{
    int16_t count = 5;
    proc_list_entry_t list[2];

    PROC1_$GET_LIST(&count, list);
    ASSERT_EQ(count, 0);
}

int main(void)
{
    RUN_TEST(test_info_illegal_pid);
    RUN_TEST(test_info_not_bound);
    RUN_TEST(test_info_current_process);
    RUN_TEST(test_info_other_process_with_stack);
    RUN_TEST(test_info_other_process_no_stack);
    RUN_TEST(test_list_selection);
    RUN_TEST(test_list_empty);
    printf("test_get_info: %d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed ? 1 : 0;
}
