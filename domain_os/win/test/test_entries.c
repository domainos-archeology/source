/*
 * win/test/test_entries.c - the small WIN jump-table entries:
 * WIN_$CINIT (0x00E30340), WIN_$DINIT (0x00E19CE8), WIN_$ERROR_QUE
 * (0x00E19D54), WIN_$GET_STATS (0x00E19D62), WIN_$SPIN_DOWN (0x00E19BC0).
 */

#include <stdio.h>
#include <string.h>

#include "win/win_internal.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  Running %-48s ", #name); \
    test_##name(); \
    tests_passed++; \
    printf("PASSED\n"); \
} while (0)

#define ASSERT_EQ(expected, actual) do { \
    long long _e = (long long)(expected); \
    long long _a = (long long)(actual); \
    if (_e != _a) { \
        printf("FAILED\n    Expected: %lld, Got: %lld at line %d\n", \
               _e, _a, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

#define ASSERT_PTR_EQ(expected, actual) do { \
    const void *_e = (const void *)(expected); \
    const void *_a = (const void *)(actual); \
    if (_e != _a) { \
        printf("FAILED\n    Expected: %p, Got: %p at line %d\n", \
               _e, _a, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

MODULE_DATA_DEFINE(win_$data_t, WIN_$DATA, 0x00E2B89C);
uint32_t win_$host_clockh(void) { return 0; }

static int8_t probe_result;
static void *probe_type, *probe_addr;
int8_t io_$probe(void *type, void *addr, void *result) { (void)result; probe_type = type; probe_addr = addr; return probe_result; }

static ec_$eventcount_t *ec_init_ec;
void EC_$INIT(ec_$eventcount_t *ec) { ec_init_ec = ec; }

static int reg_calls;
static uint16_t *reg_type, *reg_ctlr, *reg_units, *reg_flags;
static void **reg_jt;
uint8_t DISK_$REGISTER(uint16_t *type, uint16_t *controller, uint16_t *units,
                       uint16_t *flags, void **jump_table)
{
    reg_calls++; reg_type = type; reg_ctlr = controller; reg_units = units;
    reg_flags = flags; reg_jt = jump_table; return 0;
}

static int lock_calls, unlock_calls;
static int16_t lock_id, unlock_id;
void ML_$LOCK(int16_t id)   { lock_calls++; lock_id = id; }
void ML_$UNLOCK(int16_t id) { unlock_calls++; unlock_id = id; }

static uint16_t di_unit, di_sub_unit;
static status_$t di_status;
status_$t DISK_INIT(uint16_t unit, uint16_t sub_unit, int32_t *total_blocks,
                    uint16_t *blocks_per_track, uint16_t *heads,
                    uint16_t *geometry, uint16_t *drive_id)
{
    (void)total_blocks; (void)blocks_per_track; (void)heads; (void)geometry; (void)drive_id;
    di_unit = unit; di_sub_unit = sub_unit; return di_status;
}

static uint16_t ansi_unit, ansi_cmd;
static char *ansi_in;
static status_$t ansi_status;
status_$t WIN_$ANSI_COMMAND(uint16_t unit, uint16_t cmd, char *in, char *out)
{
    ansi_unit = unit; ansi_cmd = cmd; ansi_in = in; *out = 0x42; return ansi_status;
}

#include "../win_data.c"
#include "../cinit.c"
#include "../dinit.c"
#include "../error_que.c"
#include "../get_stats.c"
#include "../spin_down.c"

static dcte_t dcte;

static void reset(void)
{
    memset(WIN_$DATA.bytes, 0, sizeof(WIN_$DATA.bytes));
    memset(&dcte, 0, sizeof(dcte));
    dcte.cnum = 2;
    dcte.disk_dinit = 0x00FFDD00;
    dcte.disk_error_que = 0x00450000;
    probe_result = -1;
    reg_calls = 0;
    lock_calls = unlock_calls = 0;
    di_status = status_$ok;
    ansi_status = status_$ok;
}

/* 0x00E30350-0x00E30370: probe with the &0 cell; false = not in system. */
TEST(cinit_probe)
{
    reset();
    probe_result = 0;
    ASSERT_EQ(status_$io_controller_not_in_system, WIN_$CINIT(&dcte));
    ASSERT_PTR_EQ(&WIN_TYPE, probe_type);
    ASSERT_PTR_EQ(&dcte.disk_dinit, probe_addr);
    ASSERT_EQ(0, reg_calls);
}

/* 0x00E30372-0x00E303C4: the block's fields, the flag bits, the eventcount
 * of unit cnum, and DISK_$REGISTER's five addresses. */
TEST(cinit_registers)
{
    reset();
    WIN_$DATA.bytes[WIN_FLAGS_OFFSET] = 0x01;
    ASSERT_EQ(status_$ok, WIN_$CINIT(&dcte));
    ASSERT_EQ(ARCH_PTR_TO_VA(&dcte), *(uint32_t *)(WIN_$DATA.bytes + WIN_CTRL_INFO_OFFSET));
    ASSERT_EQ(0x00FFDD00, *(uint32_t *)(WIN_$DATA.bytes + WIN_BASE_ADDR_OFFSET));
    ASSERT_EQ(0x0045, *(uint16_t *)(WIN_$DATA.bytes + WIN_DEV_TYPE_OFFSET));
    ASSERT_EQ(0x29, WIN_$DATA.bytes[WIN_FLAGS_OFFSET]);
    ASSERT_PTR_EQ(WIN_UNIT_EC(2), ec_init_ec);
    ASSERT_EQ(1, reg_calls);
    ASSERT_PTR_EQ(&WIN_TYPE, reg_type);
    ASSERT_PTR_EQ(&WIN_TYPE, reg_ctlr);
    ASSERT_EQ(0, WIN_TYPE);
    ASSERT_PTR_EQ(WIN_$DATA.bytes + WIN_FLAGS_OFFSET, reg_units);
    ASSERT_PTR_EQ(WIN_$DATA.bytes + WIN_DEV_TYPE_OFFSET, reg_flags);
    ASSERT_EQ(ARCH_PTR_TO_VA(WIN_$DATA.bytes + WIN_JUMP_TABLE_OFFSET), *(uint32_t *)reg_jt);
}

/* 0x00E19CF6-0x00E19D48: the CONTROLLER word picks the lock record and is
 * DISK_INIT's first argument; the unit is its second. */
TEST(dinit_swaps_the_words)
{
    int32_t blocks = 0;
    uint16_t a = 0, b = 0, c = 0, d = 0;
    reset();
    WIN_UNIT_LOCK(1) = 0x66;
    di_status = 0x00080024;
    ASSERT_EQ(0x00080024, WIN_$DINIT(5, 1, &blocks, &a, &b, &c, &d));
    ASSERT_EQ(1, lock_calls);
    ASSERT_EQ(0x66, lock_id);
    ASSERT_EQ(1, di_unit);
    ASSERT_EQ(5, di_sub_unit);
    ASSERT_EQ(1, unlock_calls);
    ASSERT_EQ(0x66, unlock_id);
}

/* 0x00E19D58-0x00E19D5C */
TEST(error_que_clears_result)
{
    int8_t r = -1;
    reset();
    WIN_$ERROR_QUE(NULL, 1, &r);
    ASSERT_EQ(0, r);
}

/* 0x00E19D7C-0x00E19DAA: 22 bytes of the counters for (0,0), zeros else;
 * the 23rd/24th bytes of the destination are untouched. */
TEST(get_stats)
{
    uint8_t out[24];
    int i;
    reset();
    for (i = 0; i < 0x16; i++) WIN_$DATA.bytes[WIN_CNT_OFFSET + i] = (uint8_t)(0xA0 + i);
    memset(out, 0xEE, sizeof(out));
    WIN_$GET_STATS(0, 0, out);
    for (i = 0; i < 0x16; i++) ASSERT_EQ(0xA0 + i, out[i]);
    ASSERT_EQ(0xEE, out[22]);
    ASSERT_EQ(0xEE, out[23]);

    memset(out, 0xEE, sizeof(out));
    WIN_$GET_STATS(0, 1, out);
    for (i = 0; i < 0x16; i++) ASSERT_EQ(0, out[i]);
    ASSERT_EQ(0xEE, out[22]);
}

/* 0x00E19BCC-0x00E19BF0: SPIN CONTROL with the shared input cell; 0x14 on
 * success, else the status with its low word cleared. */
TEST(spin_down)
{
    uint16_t unit = 3;
    reset();
    ASSERT_EQ(0x14, WIN_$SPIN_DOWN(&unit));
    ASSERT_EQ(3, ansi_unit);
    ASSERT_EQ(0x55, ansi_cmd);
    ASSERT_PTR_EQ(&WIN_ANSI_IN_PARAM, ansi_in);
    ASSERT_EQ(0, WIN_ANSI_IN_PARAM);

    ansi_status = 0x00080001;
    ASSERT_EQ(0x00080000, WIN_$SPIN_DOWN(&unit));
}

int main(void)
{
    printf("WIN entry tests\n");
    RUN_TEST(cinit_probe);
    RUN_TEST(cinit_registers);
    RUN_TEST(dinit_swaps_the_words);
    RUN_TEST(error_que_clears_result);
    RUN_TEST(get_stats);
    RUN_TEST(spin_down);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
