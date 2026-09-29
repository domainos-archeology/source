/*
 * xpd/debugger.c - the debugger table
 *
 *   XPD_$FIND_DEBUGGER_INDEX   0x00E5BADC   66 bytes
 *   XPD_$REGISTER_DEBUGGER     0x00E5BB1E  186 bytes
 *   XPD_$SET_DEBUGGER          0x00E5BBD8  486 bytes
 *   XPD_$UNREGISTER_DEBUGGER   0x00E74F7C  200 bytes (segment 0x00E74F7C)
 *
 * The six slots are XPD_DEBUGGER(1..6); a slot is free when its asid word
 * is 0.  A target's slot number sits in bits 9-11 of its state word.
 */

#include "xpd/xpd_internal.h"

/*
 * XPD_$FIND_DEBUGGER_INDEX - the slot registered for an address space
 *
 * 0x00E5BAEA-0x00E5BB0A: `moveq #0x5` / `dbf` over slots 1..6 comparing the
 * asid word at +0x484 of A1 = base + 0x10*slot.  Not found -> not_a_debugger
 * and 0.
 */
int16_t XPD_$FIND_DEBUGGER_INDEX(int16_t asid, status_$t *status_ret)
{
    int16_t slot;                       /* D1 */

    for (slot = 1; slot <= XPD_MAX_DEBUGGERS; slot++) {
        if (XPD_DEBUGGER(slot)->asid == (uint16_t)asid) {
            *status_ret = status_$ok;
            return slot;
        }
    }
    *status_ret = status_$xpd_not_a_debugger;
    return 0;
}

/*
 * XPD_$REGISTER_DEBUGGER - claim a slot for an address space
 *
 * Under lock 2, one pass over the six slots remembers the first free one
 * and refuses if the asid is already registered (already_a_debugger, and
 * that slot is returned).  No free slot -> table_full and 0.  Otherwise
 * the slot gets the asid and its eventcount VALUE is zeroed (0x00E5BBBA
 * `clr.l (0x478,A3)` - only the first longword).
 *
 * Frame: D4 asid, D2 the free slot found, D3 the loop slot, A2 status.
 */
int16_t XPD_$REGISTER_DEBUGGER(int16_t asid, status_$t *status_ret)
{
    int16_t free_slot;                  /* D2 */
    int16_t slot;                       /* D3 */

    /* 0x00E5BB2A-0x00E5BB3A */
    ML_$LOCK(XPD_LOCK_ID);

    /* 0x00E5BB3C-0x00E5BB7E */
    free_slot = 0;
    for (slot = 1; slot <= XPD_MAX_DEBUGGERS; slot++) {
        if (XPD_DEBUGGER(slot)->asid == 0) {
            if (free_slot == 0) {
                free_slot = slot;
            }
        } else if (XPD_DEBUGGER(slot)->asid == (uint16_t)asid) {
            /* 0x00E5BB62-0x00E5BB76 */
            ML_$UNLOCK(XPD_LOCK_ID);
            *status_ret = status_$xpd_already_a_debugger;
            return slot;
        }
    }

    /* 0x00E5BB82-0x00E5BB9A */
    if (free_slot == 0) {
        ML_$UNLOCK(XPD_LOCK_ID);
        *status_ret = status_$xpd_debugger_table_full;
        return 0;
    }

    /* 0x00E5BB9C-0x00E5BBCC */
    XPD_DEBUGGER(free_slot)->asid = (uint16_t)asid;
    XPD_DEBUGGER(free_slot)->ec.value = 0;
    ML_$UNLOCK(XPD_LOCK_ID);
    *status_ret = status_$ok;
    return free_slot;
}

/*
 * XPD_$SET_DEBUGGER - establish or break a debugger/target link
 *
 *   target NIL, debugger NIL  -> status_$ok, nothing done
 *   target NIL                -> register the debugger's address space
 *   debugger NIL              -> detach the target (and continue it if an
 *                                event is pending)
 *   debugger == target        -> unregister the debugger
 *   otherwise                 -> put the debugger's slot into the target's
 *                                state word; if it already had one, the
 *                                new slot is written anyway and
 *                                illegal_target_setup is reported
 *
 * Frame (link.w A6,-0x20; A3 A2 D3 D2 saved):
 *   A6-0x1A  2  asid      PROC2_$FIND_ASID's result for the debugger
 *   A6-0x14  4  st        FIND_ASID's status
 *   A6-0x10  8  dbg_uid   copy of *debugger_uid
 *   A6-0x08  8  tgt_uid   copy of *target_uid
 *   D3          the target's index, D2 the debugger's slot, A2 status_ret,
 *   A3          the target record
 */
void XPD_$SET_DEBUGGER(uid_t *debugger_uid, uid_t *target_uid,
                       status_$t *status_ret)
{
    uint16_t asid;                      /* A6-0x1A */
    status_$t st;                       /* A6-0x14 */
    uid_t dbg_uid;                      /* A6-0x10 */
    uid_t tgt_uid;                      /* A6-0x08 */
    uint16_t tidx;                      /* D3 */
    int16_t slot;                       /* D2 */
    xpd_$target_t *tgt;                 /* A3 */

    /* 0x00E5BBE4-0x00E5BBF8 */
    dbg_uid = *debugger_uid;
    tgt_uid = *target_uid;

    /* 0x00E5BBFC-0x00E5BC0E: target NIL? */
    if (tgt_uid.high == UID_$NIL.high && tgt_uid.low == UID_$NIL.low) {
        /* 0x00E5BC10-0x00E5BC22: both NIL -> ok */
        if (dbg_uid.high == UID_$NIL.high && dbg_uid.low == UID_$NIL.low) {
            *status_ret = status_$ok;                   /* 0x00E5BDB2 */
            return;
        }
        /* 0x00E5BC26-0x00E5BC4E: register; the slot returned is dropped */
        asid = PROC2_$FIND_ASID(&dbg_uid, &xpd_$find_asid_flag, status_ret);
        if (*status_ret != status_$ok) {
            return;
        }
        (void)XPD_$REGISTER_DEBUGGER((int16_t)asid, status_ret);
        return;
    }

    /* 0x00E5BC52-0x00E5BC72: the target's index */
    tidx = PROC2_$FIND_ASID(&tgt_uid, &xpd_$find_asid_flag, &st);
    if (st != status_$ok) {
        *status_ret = st;
        return;
    }

    /* 0x00E5BC76-0x00E5BC88: debugger NIL -> detach */
    if (dbg_uid.high == UID_$NIL.high && dbg_uid.low == UID_$NIL.low) {
        tgt = XPD_TARGET(tidx);
        ML_$LOCK(XPD_LOCK_ID);                          /* 0x00E5BCA4 */
        /* 0x00E5BCAC-0x00E5BCB4: no slot -> unlock, ok */
        if (((tgt->state & XPD_STATE_DEBUGGER) >> XPD_STATE_DEBUGGER_SHIFT) == 0) {
            goto unlock_ok;
        }
        /* 0x00E5BCB8-0x00E5BCC8: drop the slot; no pending event -> ok */
        tgt->state &= (uint16_t)~XPD_STATE_DEBUGGER;
        if (((tgt->state & XPD_STATE_EVENT) >> XPD_STATE_EVENT_SHIFT) == 0) {
            goto unlock_ok;
        }
        /* 0x00E5BCCC-0x00E5BCE8: let it go with response 2; its status is
         * then overwritten with ok */
        ML_$UNLOCK(XPD_LOCK_ID);
        XPD_$CONTINUE_PROC(&tgt_uid, &xpd_$response_two, status_ret);
        *status_ret = status_$ok;                       /* 0x00E5BDB2 */
        return;
    }

    /* 0x00E5BCEC-0x00E5BD12: the debugger's asid; not found ->
     * debugger_not_found */
    asid = PROC2_$FIND_ASID(&dbg_uid, &xpd_$find_asid_flag, &st);
    if (st != status_$ok) {
        *status_ret = status_$xpd_debugger_not_found;
        return;
    }

    /* 0x00E5BD16-0x00E5BD34: the same process on both sides -> unregister
     * (its status is the result) */
    if (dbg_uid.high == tgt_uid.high && dbg_uid.low == tgt_uid.low) {
        XPD_$UNREGISTER_DEBUGGER((int16_t)asid, status_ret);
        return;
    }

    /* 0x00E5BD36-0x00E5BD44 */
    slot = XPD_$FIND_DEBUGGER_INDEX((int16_t)asid, status_ret);
    if (slot == 0) {
        return;
    }

    /* 0x00E5BD46-0x00E5BDA2 */
    ML_$LOCK(XPD_LOCK_ID);
    tgt = XPD_TARGET(tidx);
    if (((tgt->state & XPD_STATE_DEBUGGER) >> XPD_STATE_DEBUGGER_SHIFT) != 0) {
        /* 0x00E5BD6E-0x00E5BD92: already attached - the new slot is
         * written regardless, then illegal_target_setup */
        tgt->state = (uint16_t)((tgt->state & ~XPD_STATE_DEBUGGER) |
                                (uint16_t)(((slot << 1) & 0x0E) << 8));
        ML_$UNLOCK(XPD_LOCK_ID);
        *status_ret = status_$xpd_illegal_target_setup;
        return;
    }
    tgt->state = (uint16_t)((tgt->state & ~XPD_STATE_DEBUGGER) |
                            (uint16_t)(((slot << 1) & 0x0E) << 8));

unlock_ok:
    /* 0x00E5BDA6-0x00E5BDB2 */
    ML_$UNLOCK(XPD_LOCK_ID);
    *status_ret = status_$ok;
}

/*
 * XPD_$UNREGISTER_DEBUGGER - free an address space's slot and its targets
 *
 * Under lock 2: find the slot; clear its asid; for every target record
 * 1..57 attached to that slot, detach it and, if an event is pending,
 * continue it with response 2 (the cell at 0x00E75044; the CONTINUE status
 * goes to a local and is ignored).  Not found -> not_a_debugger.
 *
 * Frame (link.w A6,-0x1c; A4 A3 A2 D4 D3 D2 saved):
 *   A6-0x08  4  cont_status
 *   D2  asid, then the target loop counter; D3 the slot; D4 status_ret
 *   A2  &PROC2_$UNWIRED_DATA.uid[idx]; A3 the target record
 */
void XPD_$UNREGISTER_DEBUGGER(int16_t asid, status_$t *status_ret)
{
    status_$t cont_status;              /* A6-0x08 */
    int16_t slot;                       /* D3 */
    int16_t idx;
    xpd_$target_t *tgt;                 /* A3 */

    /* 0x00E74F88-0x00E74F98 */
    ML_$LOCK(XPD_LOCK_ID);

    /* 0x00E74F9A-0x00E75022: slots 1..6 */
    for (slot = 1; slot <= XPD_MAX_DEBUGGERS; slot++) {
        if (XPD_DEBUGGER(slot)->asid != (uint16_t)asid) {
            continue;
        }
        /* 0x00E74FB0 */
        XPD_DEBUGGER(slot)->asid = 0;

        /* 0x00E74FB4-0x00E75006: `moveq #0x38` / `dbf` - targets 1..57 */
        for (idx = 1; idx <= XPD_MAX_TARGETS; idx++) {
            tgt = XPD_TARGET(idx);
            if (((tgt->state & XPD_STATE_DEBUGGER) >> XPD_STATE_DEBUGGER_SHIFT) != slot) {
                continue;
            }
            tgt->state &= (uint16_t)~XPD_STATE_DEBUGGER;
            if (((tgt->state & XPD_STATE_EVENT) >> XPD_STATE_EVENT_SHIFT) != 0) {
                XPD_$CONTINUE_PROC(&PROC2_$UNWIRED_DATA.uid[idx], &xpd_$unreg_response,
                                   &cont_status);
            }
        }

        /* 0x00E7500A-0x00E75018 */
        ML_$UNLOCK(XPD_LOCK_ID);
        *status_ret = status_$ok;
        return;
    }

    /* 0x00E75026-0x00E75034 */
    ML_$UNLOCK(XPD_LOCK_ID);
    *status_ret = status_$xpd_not_a_debugger;
}
