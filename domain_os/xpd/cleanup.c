/*
 * xpd/cleanup.c - XPD_$CLEANUP (0x00E75046, 68 bytes) and XPD_$POST_EVENT
 * (0x00E75090, 166 bytes); map segment `I E74F7C XPD size = 1BC`.
 */

#include "xpd/xpd_internal.h"

/*
 * XPD_$CLEANUP - the exit path's XPD teardown for the current process
 *
 * Posts event 3 (with a zero status) to its debugger if it has one, gives
 * up its own debugger slot, and clears the enabled / debugger / event bits
 * of ITS OWN target record - the one indexed by PROC1_$AS_ID (0x00E7506C),
 * not by the PROC2 index the other routines use.
 *
 * Frame (link.w A6,-0x8): A6-0x06 the response word, A6-0x04 the status
 * XPD_$UNREGISTER_DEBUGGER writes (never read).
 */
void XPD_$CLEANUP(void)
{
    xpd_$response_t response;           /* A6-0x06 */
    status_$t unreg_status;             /* A6-0x04 */

    /* 0x00E7504A-0x00E75058: the two cells at 0x00E7508A / 0x00E7508C */
    XPD_$POST_EVENT(&xpd_$cleanup_event, &xpd_$cleanup_status, &response);

    /* 0x00E7505C-0x00E75068 (a word result slot is reserved, and left to
     * unlk) */
    XPD_$UNREGISTER_DEBUGGER((int16_t)PROC1_$AS_ID, &unreg_status);

    /* 0x00E7506C-0x00E75080: `andi.w #0x701f` keeps bits 12-14 and 0-4. */
    XPD_TARGET(PROC1_$AS_ID)->state &= 0x701F;
}

/*
 * XPD_$POST_EVENT - a target tells its debugger about an event and waits
 *
 * Frame (link.w A6,-0x4; A4 A3 A2 saved):
 *   A2  the current process's target record (PROC1_$AS_ID * 0x14)
 *   A3  response_ret
 */
void XPD_$POST_EVENT(xpd_$event_type_t *event_type, status_$t *status_val,
                     xpd_$response_t *response_ret)
{
    xpd_$target_t *tgt;                 /* A2 */
    uint16_t slot;                      /* D1 */
    uint16_t code;                      /* D1 */

    /* 0x00E75098-0x00E750C8: no debugger slot, or not enabled -> 2. */
    tgt = XPD_TARGET(PROC1_$AS_ID);
    slot = (uint16_t)((tgt->state & XPD_STATE_DEBUGGER) >> XPD_STATE_DEBUGGER_SHIFT);
    if (slot == 0 || (tgt->state & XPD_STATE_ENABLED) == 0) {
        *response_ret = 2;
        return;
    }

    /* 0x00E750CA-0x00E750EA: reset the eventcount's value, plant the event
     * code (the LOW byte of *event_type, shifted into bits 5-8 with a word
     * `or` - a code above 15 spills upward), the status, and clear ACKED. */
    tgt->ec.value = 0;
    code = (uint16_t)(*event_type & 0x00FF);
    tgt->state = (uint16_t)(tgt->state & ~XPD_STATE_EVENT);
    tgt->state |= (uint16_t)(code << XPD_STATE_EVENT_SHIFT);
    tgt->status = *status_val;
    tgt->state &= (uint16_t)~XPD_STATE_ACKED;

    /* 0x00E750F0-0x00E75108: wake the debugger */
    slot = (uint16_t)((tgt->state & XPD_STATE_DEBUGGER) >> XPD_STATE_DEBUGGER_SHIFT);
    EC_$ADVANCE(&XPD_DEBUGGER(slot)->ec);

    /* 0x00E7510A-0x00E7511C: wait for our own eventcount to reach 1; the
     * other two slots are NULL / 0 and the result is discarded. */
    (void)EC_$WAIT((ec_$wait_ecs_t){{ &tgt->ec, NULL, NULL }},
                   (ec_$wait_vals_t){{ 1, 0, 0 }});

    /* 0x00E75122-0x00E7512A: the debugger's response, bits 12-13 */
    *response_ret = (uint16_t)((tgt->state & XPD_STATE_RESPONSE) >> XPD_STATE_RESPONSE_SHIFT);
}
