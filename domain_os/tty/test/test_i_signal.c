/* tty/test/test_i_signal.c - TTY_$I_SIGNAL (0x00E1B824), TTY_$I_INTERRUPT,
 * TTY_$I_HUP and TTY_$I_DXM_SIGNAL (0x00E671DC). */
#include "tty/tty_internal.h"
#include "proc2/proc2.h"
#include "test_harness.h"

dxm_queue_t DXM_$UNWIRED_Q;
dxm_$callback_t PTR_TTY_$I_DXM_SIGNAL = 0x1234;

static void *dxm_data_seen;
static uint16_t dxm_size_seen;
static boolean dxm_dup_seen;
static const dxm_$callback_t *dxm_cb_seen;
static dxm_queue_t *dxm_q_seen;
void DXM_$ADD_CALLBACK(dxm_queue_t *queue, const dxm_$callback_t *callback,
                       void **data, uint16_t data_size, boolean check_dup, status_$t *status)
{
    dxm_q_seen = queue; dxm_cb_seen = callback; dxm_data_seen = *data;
    dxm_size_seen = data_size; dxm_dup_seen = check_dup;
    *status = 0;
    logf_call("dxm;");
}
void TTY_$I_FLUSH_INPUT(tty_desc_t *t) { (void)t; logf_call("flushin;"); }
void TTY_$I_FLUSH_OUTPUT(tty_desc_t *t) { (void)t; logf_call("flushout;"); }

static uid_t *pg_uid; static int16_t *pg_sig; static uint32_t *pg_status;
void PROC2_$SIGNAL_PGROUP_OS(uid_t *pgroup_uid, int16_t *signal, uint32_t *fault, status_$t *status)
{
    pg_uid = pgroup_uid; pg_sig = signal; pg_status = fault;
    *status = 0;
    logf_call("pgroup(%d,%lx);", *signal, (unsigned long)*fault);
}

#include "../i_signal.c"

static tty_desc_t tty;

static void reset(void)
{
    memset(&tty, 0, sizeof(tty));
    dxm_data_seen = NULL; dxm_size_seen = 0; dxm_dup_seen = 0;
    log_reset();
}

TEST(signal_index_mapping)
{
    static const struct { short sig; int idx; } map[] = {
        { 3, 0 }, { 2, 1 }, { 0x15, 2 }, { 1, 3 }, { 0x1a, 4 }, { 0x16, 5 }
    };
    unsigned i;
    for (i = 0; i < 6; i++) {
        reset();
        TTY_$I_SIGNAL(&tty, map[i].sig);
        ASSERT_STR("dxm;", call_log);
        ASSERT_EQ((unsigned long)&tty.signals[map[i].idx], (unsigned long)dxm_data_seen);
        ASSERT_EQ(12, dxm_size_seen);
        ASSERT_EQ(0xFF, (uint8_t)dxm_dup_seen);
        ASSERT_EQ((unsigned long)&DXM_$UNWIRED_Q, (unsigned long)dxm_q_seen);
        ASSERT_EQ((unsigned long)&PTR_TTY_$I_DXM_SIGNAL, (unsigned long)dxm_cb_seen);
    }
}

TEST(unknown_signal_ignored)
{
    reset();
    TTY_$I_SIGNAL(&tty, 4);
    ASSERT_STR("", call_log);
    TTY_$I_SIGNAL(&tty, 0);
    ASSERT_STR("", call_log);
}

TEST(interrupt_and_hup)
{
    reset();
    TTY_$I_INTERRUPT(&tty);
    ASSERT_STR("flushin;dxm;", call_log);
    ASSERT_EQ((unsigned long)&tty.signals[1], (unsigned long)dxm_data_seen);

    reset();
    tty.session_id = 9;
    TTY_$I_HUP(&tty);
    ASSERT_STR("flushin;flushout;dxm;dxm;", call_log);
    ASSERT_EQ(0, tty.session_id);
    ASSERT_EQ((unsigned long)&tty.signals[5], (unsigned long)dxm_data_seen);
}

TEST(dxm_callback_delivers)
{
    tty_signal_entry_t *entry;
    reset();
    ARCH_HOST_VA_BASE = (uintptr_t)&tty - 0x10000;
    tty.signals[2].tty_desc = ARCH_PTR_TO_VA(&tty);
    tty.signals[2].fault_status = 0x00120028;
    tty.signals[2].signal_num = 0x15;
    entry = &tty.signals[2];
    TTY_$I_DXM_SIGNAL(&entry);
    ASSERT_STR("pgroup(21,120028);", call_log);
    ASSERT_EQ((unsigned long)&tty.pgroup_uid, (unsigned long)pg_uid);
    ASSERT_EQ((unsigned long)&tty.signals[2].signal_num, (unsigned long)pg_sig);
    ASSERT_EQ((unsigned long)&tty.signals[2].fault_status, (unsigned long)pg_status);
}

int main(void)
{
    printf("TTY_$I_SIGNAL tests\n");
    RUN_TEST(signal_index_mapping);
    RUN_TEST(unknown_signal_ignored);
    RUN_TEST(interrupt_and_hup);
    RUN_TEST(dxm_callback_delivers);
    TEST_SUMMARY();
}
