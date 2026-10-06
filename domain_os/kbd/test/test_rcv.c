/*
 * kbd/test/test_rcv.c - Unit tests for KBD_$RCV (0x00E1CCC0), KBD_$INIT
 * (0x00E33364), KBD_$GET_DESC (0x00E1AA26), KBD_$GET_CHAR_AND_MODE
 * (0x00E724C4), KBD_$INQ_KBD_TYPE (0x00E72562), KBD_$PUT (0x00E1CEAC) and
 * KBD_$OUTPUT_BUFFER_DRAINED (0x00E1CE96)
 *
 * The real .c files are #included (kbd_data.c supplies KBD_$MODE_TABLE and
 * kbd_$escape_state; RCV's nested kbd_$process_key runs for real);
 * kbd_$state_lookup, kbd_$fetch_key,
 * kbd_$translate_key, kbd_$set_type, MMU_$NORMAL_MODE, CRASH_SYSTEM,
 * TIME_$CLOCK, DXM_$ADD_CALLBACK, EC_$* and TERM_$SET_DISCIPLINE are mocked.
 */

#include "kbd/kbd_internal.h"

#include <stdio.h>
#include <string.h>

static int tests_run = 0;
static int tests_failed = 0;
static int current_failed;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name)                                                        \
    do {                                                                      \
        current_failed = 0;                                                   \
        tests_run++;                                                          \
        test_##name();                                                        \
        printf("%-48s %s\n", #name, current_failed ? "FAIL" : "ok");          \
    } while (0)
#define ASSERT_EQ(expected, actual)                                           \
    do {                                                                      \
        long long e_ = (long long)(expected), a_ = (long long)(actual);       \
        if (e_ != a_) {                                                       \
            printf("  %s:%d: expected 0x%llx, got 0x%llx\n",                  \
                   __FILE__, __LINE__, (unsigned long long)e_,                \
                   (unsigned long long)a_);                                   \
            if (!current_failed) { current_failed = 1; tests_failed++; }      \
        }                                                                     \
    } while (0)
#define ASSERT_PTR_EQ(e, a) ASSERT_EQ((uintptr_t)(e), (uintptr_t)(a))

/* ------------------------------------------------------------ globals */

uint8_t SMD_$KTT[0x100];
uint8_t SMD_$GERMAN_KTT[0x100];
uint8_t SMD_$FRENCH_KTT[0x100];
uint8_t SMD_$SWEDISH_KTT[0x100];
uint8_t SMD_$UK_KTT[0x100];
uint8_t SMD_$SWISS_KTT[0x100];

term_data_t TERM_$DATA;
tpad_buffer_t TERM_$TPAD_BUFFER;
dxm_queue_t DXM_$UNWIRED_Q;
status_$t Term_Manual_Stop_err = 0x000B0008;

dxm_$callback_t dxm_$callback_cell(dxm_$callback_fn_t fn)
{
    return (dxm_$callback_t)(fn != NULL);
}
void TERM_$ENQUEUE_TPAD(void **p) { (void)p; }
DXM_$DEFINE_CALLBACK_CELL(PTR_TERM_$ENQUEUE_TPAD_00e1ce90, TERM_$ENQUEUE_TPAD);

/* ------------------------------------------------------------- mocks */

static kbd_$state_entry_t lookup_entry;
static uint16_t lookup_state;
static uint8_t lookup_key;
kbd_$state_entry_t *kbd_$state_lookup(uint16_t state, uint8_t key)
{
    lookup_state = state;
    lookup_key = key;
    return &lookup_entry;
}

static int8_t normal_mode;
int8_t MMU_$NORMAL_MODE(void) { return normal_mode; }

static int crash_calls;
static const status_$t *crash_cell;
void CRASH_SYSTEM(const status_$t *p) { crash_calls++; crash_cell = p; }

/* kbd_$process_key is RCV's nested procedure (static in rcv.c): with the
 * ring reset to head = tail = 1 the keys it queued are counted by the tail. */
#define process_calls   (desc.ring_tail - 1)
#define process_key_arg (desc.ring_buffer[0])

static clock_t mock_clock;
void TIME_$CLOCK(clock_t *c) { *c = mock_clock; }

static int dxm_calls;
static dxm_queue_t *dxm_queue_arg;
static const dxm_$callback_t *dxm_cb_arg;
static void *dxm_data_value;
static uint16_t dxm_size;
static boolean dxm_dup;
void DXM_$ADD_CALLBACK(dxm_queue_t *queue, const dxm_$callback_t *callback,
                       void **data, uint16_t data_size, boolean check_dup,
                       status_$t *status_ret)
{
    dxm_calls++;
    dxm_queue_arg = queue;
    dxm_cb_arg = callback;
    dxm_data_value = *data;
    dxm_size = data_size;
    dxm_dup = check_dup;
    *status_ret = status_$ok;
}

/* scripted fetch results: {result, key, mode} triples */
#define MAX_FETCH 8
static int fetch_n, fetch_i;
static int8_t fetch_res[MAX_FETCH];
static uint8_t fetch_key[MAX_FETCH];
static int16_t fetch_mode[MAX_FETCH];
int8_t kbd_$fetch_key(kbd_state_t *state, uint8_t *key_out, int16_t *mode_out)
{
    (void)state;
    if (fetch_i >= fetch_n) { return 0; }
    *key_out = fetch_key[fetch_i];
    *mode_out = fetch_mode[fetch_i];
    return fetch_res[fetch_i++];
}

uint8_t kbd_$translate_key(uint8_t key) { return (uint8_t)(key + 0x40); }

static int handler_calls;
static uint32_t handler_user;
static uint8_t handler_key[MAX_FETCH];
static int16_t test_handler(uint32_t user_data, uint8_t key)
{
    handler_user = user_data;
    if (handler_calls < MAX_FETCH) { handler_key[handler_calls] = key; }
    handler_calls++;
    return 0;
}

static int set_type_calls;
static uint8_t set_type_str[2];
static uint16_t set_type_len;
void kbd_$set_type(kbd_state_t *state, uint8_t *type_str, uint16_t type_len)
{
    (void)state;
    set_type_calls++;
    set_type_str[0] = type_str[0];
    set_type_str[1] = type_str[1];
    set_type_len = type_len;
}

static int ec_init_calls;
static ec_$eventcount_t *ec_init_arg;
void EC_$INIT(ec_$eventcount_t *ec) { ec_init_calls++; ec_init_arg = ec; }

static int ec_adv_calls;
static ec_$eventcount_t *ec_adv_arg;
void EC_$ADVANCE_WITHOUT_DISPATCH(ec_$eventcount_t *ec) { ec_adv_calls++; ec_adv_arg = ec; }

static int set_disc_calls;
static short *set_disc_line;
static int16_t set_disc_value;
void TERM_$SET_DISCIPLINE(short *line_ptr, void *discipline, status_$t *status_ret)
{
    set_disc_calls++;
    set_disc_line = line_ptr;
    set_disc_value = *(const int16_t *)discipline;
    *status_ret = status_$ok;
}

/* ---------------------------------------------------- code under test */

#include "../kbd_data.c"
#include "../crash_init.c"
#include "../rcv.c"
#include "../init.c"
#include "../get_desc.c"
#include "../get_char_and_mode.c"
#include "../inq_kbd_type.c"
#include "../put.c"
#include "../output_buffer_drained.c"

static kbd_state_t desc;
static uint8_t arena[0x100];

static void reset(void)
{
    memset(&desc, 0, sizeof(desc));
    memset(&TERM_$TPAD_BUFFER, 0, sizeof(TERM_$TPAD_BUFFER));
    memset(&TERM_$DATA, 0, sizeof(TERM_$DATA));
    memset(&lookup_entry, 0, sizeof(lookup_entry));
    ARCH_HOST_VA_BASE = (uintptr_t)arena;
    normal_mode = (int8_t)0xFF;
    crash_calls = dxm_calls = 0;
    desc.ring_head = desc.ring_tail = 1;
    desc.ring_size = KBD_RING_SIZE;
    fetch_n = fetch_i = 0;
    handler_calls = 0;
    set_type_calls = ec_init_calls = ec_adv_calls = set_disc_calls = 0;
    mock_clock = (clock_t){ 0x00010002, 0x0003 };
}

/* -------------------------------------------------------- KBD_$RCV */

TEST(rcv_plain_key_processes_and_sets_next_state)
{
    reset();
    desc.state = 3;
    lookup_entry.next = 0x15;               /* action 1, next state 5 */
    KBD_$RCV(&desc, 'a');
    ASSERT_EQ(3, lookup_state);
    ASSERT_EQ('a', lookup_key);
    ASSERT_EQ(1, process_calls);
    ASSERT_EQ('a', process_key_arg);
    ASSERT_EQ(5, desc.state);
    ASSERT_EQ(0, dxm_calls);
}

static ec_$eventcount_t key_ec;

TEST(rcv_process_key_queues_and_advances_key_ec)
{
    reset();
    desc.key_ec = &key_ec;
    lookup_entry.next = 0x10;
    KBD_$RCV(&desc, 'x');
    ASSERT_EQ(2, desc.ring_tail);
    ASSERT_EQ('x', desc.ring_buffer[0]);
    ASSERT_EQ(1, ec_adv_calls);
    ASSERT_PTR_EQ(&key_ec, ec_adv_arg);
}

TEST(rcv_process_key_wraps_tail_at_0x40)
{
    reset();
    desc.ring_head = 5;
    desc.ring_tail = KBD_RING_SIZE;
    lookup_entry.next = 0x10;
    KBD_$RCV(&desc, 'y');
    ASSERT_EQ('y', desc.ring_buffer[KBD_RING_SIZE - 1]);
    ASSERT_EQ(1, desc.ring_tail);
    ASSERT_EQ(1, ec_adv_calls);
}

TEST(rcv_process_key_drops_key_when_ring_full)
{
    reset();
    desc.ring_head = 4;
    desc.ring_tail = 3;                     /* next tail would meet head */
    lookup_entry.next = 0x10;
    KBD_$RCV(&desc, 'z');
    ASSERT_EQ(3, desc.ring_tail);
    ASSERT_EQ(0, desc.ring_buffer[2]);
    ASSERT_EQ(0, ec_adv_calls);
}

TEST(rcv_actions_a_b_c_process_and_0_7_8_9_do_nothing)
{
    static const uint8_t proc[] = { 0xA0, 0xB0, 0xC0 };
    static const uint8_t none[] = { 0x00, 0x70, 0x80, 0x90, 0xD0, 0xF0 };
    unsigned i;

    for (i = 0; i < sizeof(proc); i++) {
        reset();
        lookup_entry.next = (uint8_t)(proc[i] | 0x02);
        KBD_$RCV(&desc, 1);
        ASSERT_EQ(1, process_calls);
        ASSERT_EQ(2, desc.state);
    }
    for (i = 0; i < sizeof(none); i++) {
        reset();
        lookup_entry.next = (uint8_t)(none[i] | 0x04);
        KBD_$RCV(&desc, 1);
        ASSERT_EQ(0, process_calls);
        ASSERT_EQ(0, dxm_calls);
        ASSERT_EQ(4, desc.state);
    }
}

TEST(rcv_escape_state_from_type_table)
{
    reset();
    desc.kbd_type_idx = 2;
    lookup_entry.next = 0x0F;
    KBD_$RCV(&desc, 1);
    ASSERT_EQ(kbd_$escape_state[2], desc.state);
    ASSERT_EQ(6, desc.state);
}

TEST(rcv_manual_stop_in_normal_mode_is_a_plain_key)
{
    reset();
    lookup_entry.next = 0x21;
    normal_mode = (int8_t)0xFF;
    KBD_$RCV(&desc, 0x1B);
    ASSERT_EQ(1, process_calls);
    ASSERT_EQ(0, crash_calls);
    ASSERT_EQ(0x21, lookup_entry.next);
    ASSERT_EQ(1, desc.state);
}

TEST(rcv_manual_stop_crashes_and_patches_entry)
{
    reset();
    desc.kbd_type_idx = 1;
    lookup_entry.next = 0x21;
    normal_mode = 0;
    KBD_$RCV(&desc, 0x1B);
    ASSERT_EQ(1, crash_calls);
    ASSERT_PTR_EQ(&Term_Manual_Stop_err, crash_cell);
    ASSERT_EQ(0, process_calls);                /* bra.w past process_key */
    ASSERT_EQ(0x2F, lookup_entry.next);         /* or.w #0xF */
    ASSERT_EQ(kbd_$escape_state[1], desc.state);     /* low nibble now 0xF */
}

TEST(rcv_touchpad_bytes_land_in_order)
{
    reset();
    lookup_entry.next = 0x30;
    KBD_$RCV(&desc, 0x11);
    ASSERT_EQ(0x11, desc.tpad_x);
    ASSERT_PTR_EQ(&desc.tpad_y, desc.tpad_ptr);
    lookup_entry.next = 0x40;
    KBD_$RCV(&desc, 0x22);
    lookup_entry.next = 0x50;
    KBD_$RCV(&desc, 0x33);
    ASSERT_EQ(0x22, desc.tpad_y);
    ASSERT_EQ(0x33, desc.tpad_z);
    ASSERT_EQ(0, dxm_calls);
}

TEST(rcv_touchpad_complete_queues_sample)
{
    reset();
    desc.tpad_x = 0x11; desc.tpad_y = 0x22; desc.tpad_z = 0x33;
    desc.tpad_ptr = &desc.tpad_y;
    desc.last_time = 0x00020000;
    desc.tpad_buffer = &TERM_$TPAD_BUFFER;
    TERM_$TPAD_BUFFER.head = 2;
    TERM_$TPAD_BUFFER.tail = 0;
    mock_clock = (clock_t){ 0x00010002, 0x0005 };   /* low32 = 0x00020005 */
    lookup_entry.next = 0x60;

    KBD_$RCV(&desc, 0x44);

    ASSERT_EQ(0x44, desc.pad_29[0]);            /* tpad_ptr[2] = +0x29 */
    ASSERT_EQ(0x00010002, desc.clock_high);
    ASSERT_EQ(0x0005, desc.clock_low);
    ASSERT_EQ(5, desc.delta_time);              /* 0x20005 - 0x20000 */
    ASSERT_EQ(3, TERM_$TPAD_BUFFER.head);
    ASSERT_EQ(5, TERM_$TPAD_BUFFER.samples[2].delta_time);
    ASSERT_EQ(0x00010002, TERM_$TPAD_BUFFER.samples[2].timestamp_high);
    ASSERT_EQ(0x0005, TERM_$TPAD_BUFFER.samples[2].timestamp_low);
    ASSERT_EQ(0x11, TERM_$TPAD_BUFFER.samples[2].id_flags);
    ASSERT_EQ(0x22, TERM_$TPAD_BUFFER.samples[2].reserved_0b);
    ASSERT_EQ(0x33, TERM_$TPAD_BUFFER.samples[2].x_high);
    ASSERT_EQ(0x44, TERM_$TPAD_BUFFER.samples[2].x_low);
    ASSERT_EQ(1, dxm_calls);
    ASSERT_PTR_EQ(&DXM_$UNWIRED_Q, dxm_queue_arg);
    ASSERT_PTR_EQ(&PTR_TERM_$ENQUEUE_TPAD_00e1ce90, dxm_cb_arg);
    ASSERT_PTR_EQ(&desc.tpad_buffer, dxm_data_value);
    ASSERT_EQ(4, dxm_size);
    ASSERT_EQ((int8_t)0xFF, dxm_dup);
    ASSERT_EQ(0x00020005, desc.last_time);
    ASSERT_EQ(0, desc.state);
}

TEST(rcv_touchpad_full_buffer_only_updates_last_time)
{
    reset();
    desc.tpad_ptr = &desc.tpad_y;
    TERM_$TPAD_BUFFER.head = 5;                 /* 5 + 1 == 6 -> wraps to 0 */
    TERM_$TPAD_BUFFER.tail = 0;                 /* == new head: full */
    lookup_entry.next = 0x60;
    KBD_$RCV(&desc, 1);
    ASSERT_EQ(0, dxm_calls);
    ASSERT_EQ(5, TERM_$TPAD_BUFFER.head);
    ASSERT_EQ(0x00020003, desc.last_time);
    ASSERT_EQ(0, desc.clock_high);
}

TEST(rcv_touchpad_with_handler_is_not_queued)
{
    reset();
    desc.handler = (void *)test_handler;
    desc.tpad_ptr = &desc.tpad_y;
    lookup_entry.next = 0x60;
    KBD_$RCV(&desc, 1);
    ASSERT_EQ(0, dxm_calls);
    ASSERT_EQ(0, desc.last_time);
}

TEST(rcv_handler_drain_delivers_mode_zero_keys_only)
{
    reset();
    desc.handler = (void *)test_handler;
    desc.user_data = 0xCAFE;
    lookup_entry.next = 0x00;
    fetch_n = 3;
    fetch_res[0] = -1; fetch_key[0] = 0x01; fetch_mode[0] = 0;
    fetch_res[1] = -1; fetch_key[1] = 0x02; fetch_mode[1] = 7;   /* dropped */
    fetch_res[2] = -1; fetch_key[2] = 0x03; fetch_mode[2] = 0;
    KBD_$RCV(&desc, 1);
    ASSERT_EQ(3, fetch_i);
    ASSERT_EQ(2, handler_calls);
    ASSERT_EQ(0xCAFE, handler_user);
    ASSERT_EQ(0x41, handler_key[0]);            /* translated */
    ASSERT_EQ(0x43, handler_key[1]);
}

/* ------------------------------------------------------- KBD_$INIT */

TEST(init_seeds_descriptor)
{
    reset();
    memset(&desc, 0xAA, sizeof(desc));
    TERM_$TPAD_BUFFER.head = 3; TERM_$TPAD_BUFFER.tail = 4;
    KBD_$INIT(&desc);
    ASSERT_EQ(0, TERM_$TPAD_BUFFER.head);
    ASSERT_EQ(0, TERM_$TPAD_BUFFER.tail);
    ASSERT_EQ(0, desc.state);
    ASSERT_EQ(0, desc.sub_state);
    ASSERT_EQ(0, desc.kbd_type_idx);            /* clr.l (0x3a,A2) */
    ASSERT_EQ(0, desc.pending_mode);
    ASSERT_EQ(0, desc.flags);
    ASSERT_EQ(0xAAAAAAAA, (uint32_t)(uintptr_t)desc.handler & 0xFFFFFFFFu); /* untouched */
    ASSERT_EQ(1, set_type_calls);
    ASSERT_EQ('2', set_type_str[0]);
    ASSERT_EQ(0, set_type_str[1]);
    ASSERT_EQ(1, set_type_len);
    ASSERT_PTR_EQ(&TERM_$TPAD_BUFFER, desc.tpad_buffer);
    ASSERT_EQ(1, ec_init_calls);
    ASSERT_PTR_EQ(&desc.ec, ec_init_arg);
    ASSERT_EQ(1, desc.ring_head);
    ASSERT_EQ(1, desc.ring_tail);
    ASSERT_EQ(0x40, desc.ring_size);
    ASSERT_EQ(0x10001, desc.flags2);
    ASSERT_EQ(0x40, desc.value2);
}

/* --------------------------------------------------- KBD_$GET_DESC */

TEST(get_desc_paths)
{
    uint16_t line;
    status_$t status;
    void *r;

    reset();
    TERM_$MAX_DTTE = 2;
    TERM_$DATA.dtte[0].alt_handler = 0x10;
    TERM_$DATA.dtte[0].discipline = 0;
    TERM_$DATA.dtte[1].alt_handler = 0x20;
    TERM_$DATA.dtte[1].discipline = 0;

    line = 4; status = 0;
    (void)KBD_$GET_DESC(&line, &status);
    ASSERT_EQ(status_$invalid_line_number, status);

    line = 2; status = 0;
    (void)KBD_$GET_DESC(&line, &status);
    ASSERT_EQ(status_$requested_line_or_operation_not_implemented, status);

    line = 1; status = 0x55;
    r = KBD_$GET_DESC(&line, &status);
    ASSERT_EQ(status_$ok, status);
    ASSERT_PTR_EQ(ARCH_VA_TO_PTR(0x20), r);
    ASSERT_EQ(0, set_disc_calls);               /* line 1: no discipline check */

    line = 0; status = 0x55;
    r = KBD_$GET_DESC(&line, &status);
    ASSERT_EQ(status_$ok, status);
    ASSERT_PTR_EQ(ARCH_VA_TO_PTR(0x10), r);
    ASSERT_EQ(1, set_disc_calls);
    ASSERT_PTR_EQ(&line, set_disc_line);
    ASSERT_EQ(1, set_disc_value);               /* the WORD 0x0001 */

    TERM_$DATA.dtte[0].discipline = 1;
    (void)KBD_$GET_DESC(&line, &status);
    ASSERT_EQ(1, set_disc_calls);               /* already 1: untouched */

    TERM_$DATA.dtte[0].alt_handler = 0;
    status = 0;
    (void)KBD_$GET_DESC(&line, &status);
    ASSERT_EQ(status_$requested_line_or_operation_not_implemented, status);
}

/* ---------------------------------------- the GET_DESC-based wrappers */

TEST(get_char_and_mode_and_inq_type_and_put_and_drained)
{
    uint16_t line = 1;
    status_$t status;
    uint8_t ch = 0, mode = 0xEE, buf[4];
    uint16_t len = 0, type = 9, plen = 3;
    kbd_state_t *d;

    reset();
    TERM_$MAX_DTTE = 2;
    /* a descriptor living inside the arena, at VA 0x40 */
    d = (kbd_state_t *)(arena + 0x40);
    memset(d, 0, sizeof(*d));
    TERM_$DATA.dtte[1].alt_handler = 0x40;

    fetch_n = 1; fetch_res[0] = -1; fetch_key[0] = 'q'; fetch_mode[0] = 5;
    status = 0;
    ASSERT_EQ(-1, KBD_$GET_CHAR_AND_MODE(&line, &ch, &mode, &status));
    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ('q', ch);
    ASSERT_EQ(KBD_$MODE_TABLE[5], mode);
    ASSERT_EQ(0x10, mode);

    line = 3; status = 0; mode = 0xEE;
    ASSERT_EQ(0, KBD_$GET_CHAR_AND_MODE(&line, &ch, &mode, &status));
    ASSERT_EQ(status_$requested_line_or_operation_not_implemented, status);
    ASSERT_EQ(0xEE, mode);
    line = 1;

    d->kbd_type_str[0] = 'U'; d->kbd_type_str[1] = 'K'; d->kbd_type_len = 2;
    memset(buf, 0xAA, sizeof(buf)); status = 0;
    KBD_$INQ_KBD_TYPE(&line, buf, &len, &status);
    ASSERT_EQ('U', buf[0]); ASSERT_EQ('K', buf[1]); ASSERT_EQ(0xAA, buf[2]);
    ASSERT_EQ(2, len);
    d->kbd_type_len = 0; memset(buf, 0xAA, sizeof(buf));
    KBD_$INQ_KBD_TYPE(&line, buf, &len, &status);
    ASSERT_EQ(0xAA, buf[0]);
    ASSERT_EQ(0, len);

    status = 0;
    KBD_$PUT(&line, &type, buf, &plen, &status);
    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(0, set_type_calls);               /* the 0xE1CA8A rts */

    KBD_$OUTPUT_BUFFER_DRAINED(d);
    ASSERT_EQ(1, ec_adv_calls);
    ASSERT_PTR_EQ(&d->ec, ec_adv_arg);
}

int main(void)
{
    RUN_TEST(rcv_plain_key_processes_and_sets_next_state);
    RUN_TEST(rcv_process_key_queues_and_advances_key_ec);
    RUN_TEST(rcv_process_key_wraps_tail_at_0x40);
    RUN_TEST(rcv_process_key_drops_key_when_ring_full);
    RUN_TEST(rcv_actions_a_b_c_process_and_0_7_8_9_do_nothing);
    RUN_TEST(rcv_escape_state_from_type_table);
    RUN_TEST(rcv_manual_stop_in_normal_mode_is_a_plain_key);
    RUN_TEST(rcv_manual_stop_crashes_and_patches_entry);
    RUN_TEST(rcv_touchpad_bytes_land_in_order);
    RUN_TEST(rcv_touchpad_complete_queues_sample);
    RUN_TEST(rcv_touchpad_full_buffer_only_updates_last_time);
    RUN_TEST(rcv_touchpad_with_handler_is_not_queued);
    RUN_TEST(rcv_handler_drain_delivers_mode_zero_keys_only);
    RUN_TEST(init_seeds_descriptor);
    RUN_TEST(get_desc_paths);
    RUN_TEST(get_char_and_mode_and_inq_type_and_put_and_drained);
    printf("%d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed ? 1 : 0;
}
