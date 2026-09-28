/*
 * xpd/registers.c - register and FP access (map `I E5C1A4 XPD_KER size = 420`
 * plus GET_FP / PUT_FP / GET_TARGET_INFO from the main segment)
 *
 *   XPD_$GET_REGISTERS   0x00E5C1A4  400 bytes
 *   XPD_$PUT_REGISTERS   0x00E5C33C  404 bytes
 *   XPD_$FP_PUT_STATE    0x00E5C4D0   62 bytes
 *   XPD_$FP_GET_STATE    0x00E5C50E   76 bytes
 *   XPD_$GET_FP_INT      0x00E5C55A   52 bytes
 *   XPD_$PUT_FP_INT      0x00E5C58E   52 bytes
 *   XPD_$GET_FP          0x00E5BFFC  152 bytes
 *   XPD_$PUT_FP          0x00E5C094  152 bytes
 *   XPD_$GET_TARGET_INFO 0x00E5C12C  118 bytes
 *
 * The register sets are read from / written to the xpd_$debug_state_t
 * record a stopped target left its address in at entry+0xC6.
 */

#include "xpd/xpd_internal.h"

/* A length longword's payload in longwords: (len - 4) div 4, rounded
 * toward zero (`subq.l #4` / `bpl` / `addq.l #3` / `asr.l #2`). */
#define XPD_PAYLOAD_LONGS(len)  ((int32_t)(((int32_t)(len) - 4) / 4))

/*
 * XPD_$GET_REGISTERS
 *
 * Frame (link.w A6,-0x48; A4 A3 A2 D4 D3 D2 saved): A6-0x24 status,
 * A6-0x08 uid copy; A3 the state record; D3 status_ret.
 *
 *   mode 0  16 longwords D0-D7/A0-A7
 *   mode 1  {0xC + n + 4, SR, PC, sum, n, aux words...} - or just
 *           {0xC, SR, PC} when the aux length is <= 4
 *   mode 2  the FP buffer, length first
 *   mode 3  {fault status with bit 23 cleared, signal, last PC (only for a
 *           single-step stop), sig_mask_2}
 */
void XPD_$GET_REGISTERS(uid_t *proc_uid, int16_t *mode, void *regs,
                        status_$t *status_ret)
{
    status_$t st;                       /* A6-0x24 */
    uid_t uid;                          /* A6-0x08 */
    int16_t idx;                        /* D2 */
    proc2_info_t *entry;                /* A0 */
    xpd_$debug_state_t *state;          /* A3 */
    uint32_t *out;                      /* A2 / A4 / A1 */
    uint32_t *aux;                      /* A1 */
    uint32_t *fp;                       /* A2 */
    int32_t n;                          /* D0 */
    int16_t i;
    uint32_t fault;

    /* 0x00E5C1B0-0x00E5C1EE */
    uid = *proc_uid;
    ML_$LOCK(PROC2_LOCK_ID);
    idx = XPD_$FIND_INDEX(&uid, &st);
    ML_$UNLOCK(PROC2_LOCK_ID);
    if (st != status_$ok) {
        goto done;
    }

    /* 0x00E5C1F2-0x00E5C214: no record -> state_unavailable, stored
     * directly (the tail is skipped) */
    entry = XPD_ENTRY(idx);
    state = (xpd_$debug_state_t *)ARCH_VA_TO_PTR(XPD_ENTRY_STATE_VA(entry));
    if (state == NULL) {
        *status_ret = status_$xpd_state_unavailable_for_this_event;
        return;
    }

    /* 0x00E5C218-0x00E5C22C: the jump table at 0x00E5C230 */
    switch ((uint16_t)*mode) {
    case XPD_REG_MODE_GENERAL:
        /* 0x00E5C238-0x00E5C24E: `moveq #0xf` / `dbf` */
        out = (uint32_t *)regs;
        for (i = 0; i < 16; i++) {
            out[i] = state->regs[i];
        }
        break;

    case XPD_REG_MODE_EXCEPTION:
        /* 0x00E5C252-0x00E5C2B4 */
        out = (uint32_t *)regs;
        aux = state->aux;
        out[0] = 0xC;
        out[1] = state->frame->sr;
        out[2] = XPD_FRAME_PC(state->frame);
        if (aux[0] <= 4) {
            break;
        }
        out[0] = 0xC + aux[0] + 4;
        out[3] = 0;
        out[4] = aux[0];
        n = XPD_PAYLOAD_LONGS(aux[0]);
        for (i = 0; i < (int16_t)n; i++) {
            out[3] += aux[1 + i];
            out[5 + i] = aux[1 + i];
        }
        break;

    case XPD_REG_MODE_FP_STATE:
        /* 0x00E5C2B6-0x00E5C2E6 */
        out = (uint32_t *)regs;
        fp = state->fp_buf;
        out[0] = fp[0];
        n = XPD_PAYLOAD_LONGS(fp[0]);
        for (i = 0; i < (int16_t)n; i++) {
            out[1 + i] = fp[1 + i];
        }
        break;

    case XPD_REG_MODE_DEBUG_STATE:
        /* 0x00E5C2E8-0x00E5C322: entry+0x94's low byte, entry+0xC2 with
         * bit 23 dropped (`bclr.b #0x7,(0x1,A1)`), entry+0x80, and the
         * last PC when the raw +0xC2 is a single-step stop */
        out = (uint32_t *)regs;
        out[1] = (uint32_t)(entry->pad_94 & 0x00FF);
        fault = PROC2_FAULT_PARAM_GET(entry);
        out[0] = fault & ~0x00800000u;
        out[3] = entry->sig_mask_2;
        if (fault == status_$fault_single_step_completed) {
            out[2] = XPD_ENTRY_LAST_PC(entry);
        } else {
            out[2] = 0;
        }
        break;

    default:
        /* 0x00E5C324 */
        st = status_$xpd_invalid_option;
        break;
    }

done:
    /* 0x00E5C32C-0x00E5C32E */
    *status_ret = st;
}

/*
 * XPD_$PUT_REGISTERS
 *
 * Frame (link.w A6,-0x44; A3 A2 D5 D4 D3 D2 saved): A6-0x28 status,
 * A6-0x08 uid copy; D5 the state record; D4 status_ret.
 *
 * Quirk reproduced: only the LOAD of the record is skipped on a bad
 * XPD_$FIND_INDEX status (0x00E5C386 `bne 0x00E5C39C`); the mode switch
 * still runs with D5 uninitialised.
 *
 *   mode 0  16 longwords into the register block
 *   mode 1  {len, SR, PC, sum, n, words...}: with len > 0xC the words go to
 *           the aux buffer after checking their sum and n <= 0xD4
 *           (invalid_state_argument otherwise); then SR/PC into the frame
 *   mode 2  the FP buffer, n <= 0x6C
 * Each successful mode 1/2 store sets the record's fp_modified byte.
 */
void XPD_$PUT_REGISTERS(uid_t *proc_uid, int16_t *mode, void *regs,
                        status_$t *status_ret)
{
    status_$t st;                       /* A6-0x28 */
    uid_t uid;                          /* A6-0x08 */
    int16_t idx;                        /* D2 */
    xpd_$debug_state_t *state;          /* D5 - see the quirk above */
    const uint32_t *in;                 /* A2 */
    uint32_t *aux;                      /* A3 */
    uint32_t *fp;                       /* A3 */
    int32_t n;                          /* D0 */
    uint32_t sum;                       /* D1 */
    int16_t i;

    /* 0x00E5C344-0x00E5C382 */
    uid = *proc_uid;
    ML_$LOCK(PROC2_LOCK_ID);
    idx = XPD_$FIND_INDEX(&uid, &st);
    ML_$UNLOCK(PROC2_LOCK_ID);
    if (st == status_$ok) {
        /* 0x00E5C388-0x00E5C398 */
        state = (xpd_$debug_state_t *)ARCH_VA_TO_PTR(XPD_ENTRY_STATE_VA(XPD_ENTRY(idx)));
    }

    /* 0x00E5C39C-0x00E5C3B2 */
    switch ((uint16_t)*mode) {
    case XPD_REG_MODE_GENERAL:
        /* 0x00E5C3B6-0x00E5C3CE */
        in = (const uint32_t *)regs;
        for (i = 0; i < 16; i++) {
            state->regs[i] = in[i];
        }
        break;

    case XPD_REG_MODE_EXCEPTION:
        /* 0x00E5C3D2-0x00E5C3DE */
        in = (const uint32_t *)regs;
        if (in[0] <= 0xC) {
            /* 0x00E5C450-0x00E5C458 */
            state->aux[0] = 0;
            state->fp_modified = -1;
        } else {
            /* 0x00E5C3E0-0x00E5C410: n words follow the five header
             * longwords; their sum must equal in[3] and n <= 0xD4 */
            n = XPD_PAYLOAD_LONGS(in[4]);
            sum = 0;
            for (i = 0; i < (int16_t)n; i++) {
                sum += in[5 + i];
            }
            if (sum != in[3] || !((int16_t)n <= 0xD4)) {
                *status_ret = status_$xpd_invalid_state_argument;  /* 0x00E5C414 */
                return;
            }
            /* 0x00E5C41E-0x00E5C44A */
            aux = state->aux;
            for (i = 0; i < (int16_t)n; i++) {
                aux[1 + i] = in[5 + i];
            }
            aux[0] = in[4];
            state->fp_modified = -1;
        }
        /* 0x00E5C45C-0x00E5C464: SR from in[1]'s low word, PC from in[2]
         * (`move.l (0x6,A2),(A1)` / `move.w (0xa,A2),(0x4,A1)`) */
        state->frame->sr = (uint16_t)in[1];
        XPD_FRAME_PC_SET(state->frame, in[2]);
        break;

    case XPD_REG_MODE_FP_STATE:
        /* 0x00E5C46C-0x00E5C4B4 */
        in = (const uint32_t *)regs;
        fp = state->fp_buf;
        fp[0] = in[0];
        n = XPD_PAYLOAD_LONGS(in[0]);
        if (!((int16_t)n <= 0x6C)) {
            *status_ret = status_$xpd_invalid_state_argument;      /* 0x00E5C490 */
            return;
        }
        for (i = 0; i < (int16_t)n; i++) {
            fp[1 + i] = in[1 + i];
        }
        state->fp_modified = -1;
        break;

    default:
        /* 0x00E5C4B6 */
        st = status_$xpd_invalid_option;
        break;
    }

    /* 0x00E5C4C0-0x00E5C4C2 */
    *status_ret = st;
}

/*
 * XPD_$FP_PUT_STATE - restore a target's FP state from the save area
 *
 * MC68881 present: FIM_$FP_PUT_STATE(buf, aux); else a PEB: PEB_$LOAD_REGS
 * from buf+4; else nothing.
 */
void XPD_$FP_PUT_STATE(void *fp_buf, void *aux)
{
    /* 0x00E5C4DA-0x00E5C500 */
    if (M68881_$SAVE_FLAG < 0) {
        FIM_$FP_PUT_STATE(fp_buf, (status_$t *)aux);
    } else if (PEB_$INSTALLED_FLAG < 0) {
        PEB_$LOAD_REGS((peb_fp_state_t *)((uint8_t *)fp_buf + 4));
    }
}

/*
 * XPD_$FP_GET_STATE - save a target's FP state into the save area
 *
 * *aux is zeroed first.  MC68881: FIM_$FP_GET_STATE(buf, aux).  PEB:
 * PEB_$UNLOAD_REGS into buf+4, then buf[0] = 0x20 and *aux = 4.
 */
void XPD_$FP_GET_STATE(void *fp_buf, void *aux)
{
    /* 0x00E5C51E-0x00E5C54E */
    *(uint32_t *)aux = 0;
    if (M68881_$SAVE_FLAG < 0) {
        FIM_$FP_GET_STATE(fp_buf, (status_$t *)aux);
    } else if (PEB_$INSTALLED_FLAG < 0) {
        PEB_$UNLOAD_REGS((peb_fp_state_t *)((uint8_t *)fp_buf + 4));
        *(uint32_t *)fp_buf = 0x20;
        *(uint32_t *)aux = 4;
    }
}

/*
 * XPD_$GET_FP_INT / XPD_$PUT_FP_INT - hand an address space's FP context
 * to the right owner: FP_$GET_FP / FP_$PUT_FP (by value, a word result slot
 * discarded) with a 68881, else the PEB (by address).  *status is zeroed.
 */
void XPD_$GET_FP_INT(int16_t *asid, status_$t *status_ret)
{
    /* 0x00E5C564-0x00E5C584 */
    *status_ret = status_$ok;
    if (M68881_$SAVE_FLAG < 0) {
        FP_$GET_FP((uint16_t)*asid);
    } else {
        PEB_$GET_FP(asid);
    }
}

void XPD_$PUT_FP_INT(int16_t *asid, status_$t *status_ret)
{
    /* 0x00E5C598-0x00E5C5B8 */
    *status_ret = status_$ok;
    if (M68881_$SAVE_FLAG < 0) {
        FP_$PUT_FP((uint16_t)*asid);
    } else {
        PEB_$PUT_FP(asid);
    }
}

/*
 * XPD_$GET_FP / XPD_$PUT_FP - the FP context of a target of the caller
 *
 * The target's slot must belong to the caller's address space, an event
 * must be pending, the target enabled, and the slot non-zero; otherwise
 * target_not_suspended.  The index PROC2_$FIND_ASID returned is handed to
 * the _INT routine by address (A6-0x02).  A5 = 0x00E81814, unused.
 */
void XPD_$GET_FP(uid_t *proc_uid, status_$t *status_ret)
{
    int16_t idx;                        /* D2 / A6-0x02 */
    xpd_$target_t *tgt;                 /* A0 */
    uint16_t slot;                      /* D0 */

    /* 0x00E5C00E-0x00E5C024 */
    idx = (int16_t)PROC2_$FIND_ASID(proc_uid, &xpd_$find_asid_flag, status_ret);
    if (idx == 0) {
        return;
    }
    /* 0x00E5C026-0x00E5C070 */
    tgt = XPD_TARGET(idx);
    slot = (uint16_t)((tgt->state & XPD_STATE_DEBUGGER) >> XPD_STATE_DEBUGGER_SHIFT);
    if (XPD_DEBUGGER(slot)->asid != PROC1_$AS_ID ||
        ((tgt->state & XPD_STATE_EVENT) >> XPD_STATE_EVENT_SHIFT) == 0 ||
        (tgt->state & XPD_STATE_ENABLED) == 0 ||
        slot == 0) {
        *status_ret = status_$xpd_target_not_suspended;
        return;
    }
    /* 0x00E5C07A-0x00E5C084 */
    XPD_$GET_FP_INT(&idx, status_ret);
}

void XPD_$PUT_FP(uid_t *proc_uid, status_$t *status_ret)
{
    int16_t idx;                        /* D2 / A6-0x02 */
    xpd_$target_t *tgt;                 /* A0 */
    uint16_t slot;                      /* D0 */

    /* 0x00E5C0A6-0x00E5C0BC */
    idx = (int16_t)PROC2_$FIND_ASID(proc_uid, &xpd_$find_asid_flag, status_ret);
    if (idx == 0) {
        return;
    }
    /* 0x00E5C0BE-0x00E5C108 */
    tgt = XPD_TARGET(idx);
    slot = (uint16_t)((tgt->state & XPD_STATE_DEBUGGER) >> XPD_STATE_DEBUGGER_SHIFT);
    if (XPD_DEBUGGER(slot)->asid != PROC1_$AS_ID ||
        ((tgt->state & XPD_STATE_EVENT) >> XPD_STATE_EVENT_SHIFT) == 0 ||
        (tgt->state & XPD_STATE_ENABLED) == 0 ||
        slot == 0) {
        *status_ret = status_$xpd_target_not_suspended;
        return;
    }
    /* 0x00E5C112-0x00E5C11C */
    XPD_$PUT_FP_INT(&idx, status_ret);
}

/*
 * XPD_$GET_TARGET_INFO - is this a target, and is it stopped?
 *
 * *is_target = ENABLED (`smi`) AND slot != 0 (`sne`), as Domain booleans
 * `and.b`ed together; *is_suspended = (event code != 0) only when
 * *is_target is true, else 0.  A5 = 0x00E81814, unused.
 */
void XPD_$GET_TARGET_INFO(uid_t *proc_uid, int8_t *is_target,
                          int8_t *is_suspended, status_$t *status_ret)
{
    uint16_t idx;                       /* D0 */
    xpd_$target_t *tgt;                 /* A0 */
    int8_t enabled;                     /* D1 */
    int8_t has_slot;                    /* D2 */

    /* 0x00E5C142-0x00E5C15A */
    idx = PROC2_$FIND_ASID(proc_uid, &xpd_$find_asid_flag, status_ret);
    if (idx == 0) {
        return;
    }
    /* 0x00E5C15C-0x00E5C184 */
    tgt = XPD_TARGET(idx);
    enabled = ((tgt->state & XPD_STATE_ENABLED) != 0) ? -1 : 0;
    has_slot = (((tgt->state & XPD_STATE_DEBUGGER) >> XPD_STATE_DEBUGGER_SHIFT) != 0) ? -1 : 0;
    *is_target = (int8_t)(enabled & has_slot);
    /* 0x00E5C184-0x00E5C196 */
    if (*is_target < 0) {
        *is_suspended = (((tgt->state & XPD_STATE_EVENT) >> XPD_STATE_EVENT_SHIFT) != 0) ? -1 : 0;
    } else {
        *is_suspended = 0;
    }
}
