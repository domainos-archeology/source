/*
 * smd/test/test_blt.c - Unit tests for SMD_$BLT
 *
 * Tests BLT parameter conversion, mode validation, lock selection,
 * sync vs async behavior, and the unit==0 error path.
 */

#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>

/* Minimal type stubs for native compilation */
typedef long status_$t;
#define status_$ok 0
#define status_$display_invalid_use_of_driver_procedure 0x00130004
#define status_$display_invalid_blt_op 0x00130028

/* Test result tracking */
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  Running %s... ", #name); \
    test_##name(); \
    tests_passed++; \
    printf("PASSED\n"); \
} while(0)

#define ASSERT_EQ(expected, actual) do { \
    if ((expected) != (actual)) { \
        printf("FAILED\n    Expected: 0x%lx, Got: 0x%lx at line %d\n", \
               (unsigned long)(expected), (unsigned long)(actual), __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_NEQ(not_expected, actual) do { \
    if ((not_expected) == (actual)) { \
        printf("FAILED\n    Expected not 0x%lx at line %d\n", \
               (unsigned long)(not_expected), __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

/*
 * Mock data structures
 */

/* Event count - minimal mock (12 bytes like ec_$eventcount_t) */
typedef struct {
    uint32_t value;
    uint32_t head;
    uint32_t tail;
} mock_ec_t;

/* Display hardware info - minimal mock matching smd_display_hw_t */
typedef struct {
    uint16_t display_type;
    uint16_t lock_state;
    mock_ec_t lock_ec;    /* 0x04 */
    mock_ec_t op_ec;      /* 0x10 */
} mock_hw_t;

/* Display unit - minimal mock matching smd_display_unit_t */
typedef struct {
    mock_ec_t event_count_1;          /* 0x00 */
    void *hdm_list_ptr;               /* 0x0C */
    uint16_t field_10;                /* 0x10 */
    uint16_t asid;                    /* 0x12 */
    uint16_t field_14;                /* 0x14 */
    uint16_t field_16;                /* 0x16 */
    mock_hw_t *hw;                    /* 0x18 */
    uint32_t field_1c;                /* 0x1C */
    uint32_t field_20;                /* 0x20 */
} mock_display_unit_t;

/* Unit auxiliary data */
typedef struct {
    mock_hw_t *hw;
    uint16_t owner_asid;
    uint16_t borrowed_asid;
} mock_unit_aux_t;

/* SMD globals - minimal */
#define MOCK_MAX_ASIDS 256
typedef struct {
    uint8_t pad_00[0x48];
    uint16_t asid_to_unit[MOCK_MAX_ASIDS];
} mock_smd_globals_t;

/* Mock globals */
static mock_smd_globals_t mock_globals;
static mock_hw_t mock_hw;
static mock_unit_aux_t mock_unit_aux;
static uint16_t mock_as_id;
static int16_t mock_blt_async_lock_data;
static int16_t mock_blt_sync_lock_data;

/* Tracking variables for mock calls */
static int16_t *last_acq_lock_data;
static int acq_display_called;
static int rel_display_called;
static int start_blt_called;
static uint16_t *last_start_blt_params;
static mock_hw_t *last_start_blt_hw;
static uint16_t *last_start_blt_hw_regs;

/*
 * Redefine external references to use mocks
 */
#define SMD_GLOBALS mock_globals
#define PROC1_$AS_ID mock_as_id
#define SMD_EC_1 mock_ec_1
#define SMD_DISPLAY_UNIT_SIZE sizeof(mock_display_unit_t)
#define SMD_BLT_ASYNC_LOCK_DATA mock_blt_async_lock_data
#define SMD_BLT_SYNC_LOCK_DATA mock_blt_sync_lock_data

/*
 * We override address-based display_unit computation to use our mock.
 * The real code does: (uint8_t *)&SMD_EC_1 + unit_offset
 * Since our mock unit size matches, this should land on mock_display_unit
 * as long as we arrange memory correctly.
 *
 * For simplicity, we'll set up so that unit=1 maps to mock_display_unit
 * by placing mock_ec_1 right before mock_display_unit in a combined array.
 */
static struct {
    mock_display_unit_t units[2]; /* unit 0 = EC_1 base, unit 1 = actual display unit */
} mock_unit_array;

/* Redirect SMD_EC_1 to be the first element of our array */
#undef SMD_EC_1
#define SMD_EC_1 mock_unit_array.units[0].event_count_1

/* Mock smd_get_unit_aux */
static mock_unit_aux_t *smd_get_unit_aux(uint16_t unit_num) {
    (void)unit_num;
    return &mock_unit_aux;
}

/* Mock SMD_$ACQ_DISPLAY */
static uint16_t SMD_$ACQ_DISPLAY(int16_t *lock_data) {
    last_acq_lock_data = lock_data;
    acq_display_called++;
    return 0;
}

/* Mock SMD_$REL_DISPLAY */
static void SMD_$REL_DISPLAY(void) {
    rel_display_called++;
}

/* Mock SMD_$START_BLT */
static void SMD_$START_BLT(uint16_t *params, mock_hw_t *hw, uint16_t *hw_regs) {
    last_start_blt_params = params;
    last_start_blt_hw = hw;
    last_start_blt_hw_regs = hw_regs;
    start_blt_called++;
}

/* Typedefs to satisfy function under test */
typedef mock_ec_t ec_$eventcount_t;
typedef mock_hw_t smd_display_hw_t;
typedef mock_unit_aux_t smd_unit_aux_t;
typedef mock_display_unit_t smd_display_unit_t;

/*
 * Hardware BLT parameter structure (from blt.c)
 */
typedef struct smd_hw_blt_t {
    uint16_t    control;
    uint16_t    bit_pos;
    uint16_t    mask;
    uint16_t    pattern;
    uint16_t    y_extent;
    uint16_t    x_extent;
    uint16_t    y_start;
    uint16_t    x_start;
} smd_hw_blt_t;

/*
 * Function under test - reimplemented with mocks
 */
void SMD_$BLT(uint16_t *params, uint32_t param2, uint32_t param3, status_$t *status_ret)
{
    uint16_t unit;
    int32_t unit_offset;
    smd_display_hw_t *hw;
    smd_display_unit_t *display_unit;
    smd_unit_aux_t *aux;
    uint16_t mode;
    int16_t *lock_data;
    smd_hw_blt_t hw_params;
    int16_t dx, dy;

    (void)param2;
    (void)param3;

    unit = SMD_GLOBALS.asid_to_unit[PROC1_$AS_ID];

    if (unit == 0) {
        *status_ret = status_$display_invalid_use_of_driver_procedure;
        return;
    }

    mode = params[0];

    unit_offset = (int32_t)unit * SMD_DISPLAY_UNIT_SIZE;
    display_unit = (smd_display_unit_t *)((uint8_t *)&SMD_EC_1 + unit_offset);
    aux = smd_get_unit_aux(unit);
    hw = aux->hw;

    if ((mode & 0x10) != 0) {
        lock_data = &SMD_BLT_ASYNC_LOCK_DATA;
    } else {
        lock_data = &SMD_BLT_SYNC_LOCK_DATA;
    }

    SMD_$ACQ_DISPLAY(lock_data);

    if ((int8_t)mode < 0 || (mode & 0x40) != 0 || (mode & 0x08) != 0) {
        *status_ret = status_$display_invalid_blt_op;
        SMD_$REL_DISPLAY();
        return;
    }

    hw_params.control = ((mode & 0x8000) ? 0x80 : 0) |
                        ((mode & 0x20) ? 0x20 : 0) |
                        ((mode & 0x10) ? 0x10 : 0) |
                        ((((uint8_t *)&params[1])[3] == 0x02) ? 0x08 : 0) |
                        ((((uint8_t *)&params[1])[0] == 0x20) ? 0x04 : 0) |
                        ((mode & 0x02) ? 0x02 : 0) |
                        ((mode & 0x01) ? 0x01 : 0);

    hw_params.bit_pos = params[12] & 0x0F;
    hw_params.pattern = params[5];
    hw_params.mask = params[6];

    dy = params[11] - params[7];
    if (dy < 0) dy = -dy;
    hw_params.y_extent = -1 - dy;

    dx = (params[12] >> 4) - (params[8] >> 4);
    if (dx < 0) dx = -dx;
    hw_params.x_extent = -1 - dx;

    hw_params.y_start = params[7];
    hw_params.x_start = params[8];

    SMD_$START_BLT((uint16_t *)&hw_params, hw,
                   (uint16_t *)((uint8_t *)&SMD_EC_1 + unit_offset + 8));

    if ((mode & 0x10) == 0) {
        SMD_$REL_DISPLAY();
    } else {
        display_unit->asid = PROC1_$AS_ID;
    }

    *status_ret = status_$ok;
}

/*
 * Test setup helper
 */
static void setup(void)
{
    memset(&mock_globals, 0, sizeof(mock_globals));
    memset(&mock_hw, 0, sizeof(mock_hw));
    memset(&mock_unit_aux, 0, sizeof(mock_unit_aux));
    memset(&mock_unit_array, 0, sizeof(mock_unit_array));

    mock_unit_aux.hw = &mock_hw;
    mock_as_id = 1;
    mock_globals.asid_to_unit[1] = 1; /* ASID 1 -> unit 1 */
    mock_blt_async_lock_data = 0;
    mock_blt_sync_lock_data = 0;

    last_acq_lock_data = NULL;
    acq_display_called = 0;
    rel_display_called = 0;
    start_blt_called = 0;
    last_start_blt_params = NULL;
    last_start_blt_hw = NULL;
    last_start_blt_hw_regs = NULL;
}

/*
 * Tests
 */

TEST(unit_zero_error)
{
    setup();
    mock_globals.asid_to_unit[1] = 0; /* No display for this ASID */
    uint16_t params[13] = {0};
    status_$t st = -1;

    SMD_$BLT(params, 0, 0, &st);

    ASSERT_EQ(status_$display_invalid_use_of_driver_procedure, st);
    ASSERT_EQ(0, acq_display_called);
    ASSERT_EQ(0, start_blt_called);
}

TEST(invalid_mode_bit7)
{
    setup();
    uint16_t params[13] = {0};
    params[0] = 0x0080; /* bit 7 set */
    status_$t st = -1;

    SMD_$BLT(params, 0, 0, &st);

    ASSERT_EQ(status_$display_invalid_blt_op, st);
    ASSERT_EQ(1, acq_display_called);
    ASSERT_EQ(1, rel_display_called); /* Lock released on error */
    ASSERT_EQ(0, start_blt_called);
}

TEST(invalid_mode_bit6)
{
    setup();
    uint16_t params[13] = {0};
    params[0] = 0x0040; /* bit 6 set */
    status_$t st = -1;

    SMD_$BLT(params, 0, 0, &st);

    ASSERT_EQ(status_$display_invalid_blt_op, st);
    ASSERT_EQ(1, rel_display_called);
    ASSERT_EQ(0, start_blt_called);
}

TEST(invalid_mode_bit3)
{
    setup();
    uint16_t params[13] = {0};
    params[0] = 0x0008; /* bit 3 set */
    status_$t st = -1;

    SMD_$BLT(params, 0, 0, &st);

    ASSERT_EQ(status_$display_invalid_blt_op, st);
    ASSERT_EQ(1, rel_display_called);
    ASSERT_EQ(0, start_blt_called);
}

TEST(sync_mode_uses_sync_lock)
{
    setup();
    uint16_t params[13] = {0};
    params[0] = 0x0000; /* sync mode (bit 4 clear) */
    status_$t st = -1;

    SMD_$BLT(params, 0, 0, &st);

    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ((unsigned long)&mock_blt_sync_lock_data, (unsigned long)last_acq_lock_data);
    ASSERT_EQ(1, rel_display_called); /* Sync releases lock */
}

TEST(async_mode_uses_async_lock)
{
    setup();
    uint16_t params[13] = {0};
    params[0] = 0x0010; /* async mode (bit 4 set) */
    status_$t st = -1;

    SMD_$BLT(params, 0, 0, &st);

    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ((unsigned long)&mock_blt_async_lock_data, (unsigned long)last_acq_lock_data);
    ASSERT_EQ(0, rel_display_called); /* Async does NOT release lock */
}

TEST(async_records_asid)
{
    setup();
    uint16_t params[13] = {0};
    params[0] = 0x0010; /* async mode */
    status_$t st = -1;

    SMD_$BLT(params, 0, 0, &st);

    ASSERT_EQ(status_$ok, st);
    /* Verify the async path sets display_unit->asid to the current ASID */
    ASSERT_EQ(mock_as_id, mock_unit_array.units[1].asid);
}

TEST(hw_pointer_from_aux)
{
    setup();
    uint16_t params[13] = {0};
    params[0] = 0x0000;
    status_$t st = -1;

    SMD_$BLT(params, 0, 0, &st);

    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(1, start_blt_called);
    /* Verify hw pointer came from aux->hw, not from direct address casting */
    ASSERT_EQ((unsigned long)&mock_hw, (unsigned long)last_start_blt_hw);
}

TEST(basic_blt_params)
{
    setup();
    uint16_t params[13] = {0};
    /* mode = 0x0003 (src+dest enable) */
    params[0] = 0x0003;
    /* params[5] = pattern, params[6] = mask */
    params[5] = 0x1234;
    params[6] = 0x5678;
    /* y_start=10, x_start=0x0050 */
    params[7] = 10;
    params[8] = 0x0050;
    /* y_end=20, x_end with bit_pos */
    params[11] = 20;
    params[12] = 0x00A3; /* x_end = 0x0A << 4 = 0x00A0, bit_pos = 3 */
    status_$t st = -1;

    SMD_$BLT(params, 0, 0, &st);

    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(1, start_blt_called);

    /* Verify the hw_params passed to SMD_$START_BLT */
    smd_hw_blt_t *hw_p = (smd_hw_blt_t *)last_start_blt_params;
    ASSERT_EQ(0x0003, hw_p->control);     /* src+dest enable */
    ASSERT_EQ(3, hw_p->bit_pos);          /* low nibble of params[12] */
    ASSERT_EQ(0x1234, hw_p->pattern);
    ASSERT_EQ(0x5678, hw_p->mask);
    ASSERT_EQ(10, hw_p->y_start);
    ASSERT_EQ(0x0050, hw_p->x_start);

    /* y_extent = -1 - |20-10| = -11 = 0xFFF5 */
    ASSERT_EQ((uint16_t)(-11), hw_p->y_extent);

    /* dx = (0x00A3 >> 4) - (0x0050 >> 4) = 0x0A - 0x05 = 5 */
    /* x_extent = -1 - 5 = -6 = 0xFFFA */
    ASSERT_EQ((uint16_t)(-6), hw_p->x_extent);
}

TEST(control_word_alt_rop)
{
    setup();
    uint16_t params[13] = {0};
    params[0] = 0x0020; /* alt ROP (bit 5) */
    status_$t st = -1;

    SMD_$BLT(params, 0, 0, &st);

    ASSERT_EQ(status_$ok, st);
    smd_hw_blt_t *hw_p = (smd_hw_blt_t *)last_start_blt_params;
    ASSERT_EQ(0x0020, hw_p->control);
}

int main(void)
{
    printf("test_blt:\n");

    RUN_TEST(unit_zero_error);
    RUN_TEST(invalid_mode_bit7);
    RUN_TEST(invalid_mode_bit6);
    RUN_TEST(invalid_mode_bit3);
    RUN_TEST(sync_mode_uses_sync_lock);
    RUN_TEST(async_mode_uses_async_lock);
    RUN_TEST(async_records_asid);
    RUN_TEST(hw_pointer_from_aux);
    RUN_TEST(basic_blt_params);
    RUN_TEST(control_word_alt_rop);

    printf("\n  Results: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
