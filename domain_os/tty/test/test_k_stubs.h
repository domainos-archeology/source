/* Shared stubs for the TTY_$K_* tests: one static descriptor returned by a
 * scripted TTY_$I_GET_DESC, plus recording lock/unlock stubs. */
#ifndef TTY_TEST_K_STUBS_H
#define TTY_TEST_K_STUBS_H
#include "tty/tty_internal.h"
#include "test_harness.h"

static tty_desc_t tty;
static status_$t desc_status;
tty_desc_t *TTY_$I_GET_DESC(short line, status_$t *status)
{ logf_call("desc(%d);", line); *status = desc_status; return &tty; }
void TTY_$I_LOCK(tty_desc_t *t) { (void)t; logf_call("lock;"); }
void TTY_$I_UNLOCK(tty_desc_t *t) { (void)t; logf_call("unlock;"); }
uint32_t TTY_$SPIN_LOCK;
ml_$spin_token_t ML_$SPIN_LOCK(void *p) { (void)p; logf_call("spin;"); return 3; }
void ML_$SPIN_UNLOCK(void *p, ml_$spin_token_t t) { (void)p; (void)t; logf_call("unspin;"); }

static void k_reset(void)
{
    memset(&tty, 0, sizeof(tty));
    desc_status = status_$ok;
    log_reset();
}
#endif
