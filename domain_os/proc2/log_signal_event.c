/*
 * PROC2_$LOG_SIGNAL_EVENT - Audit a signal delivery
 *
 * Re-emitted from the image (0x00E3E748..0x00E3E804, 190 bytes).
 *
 * Frame (link.w A6,-0x1C):
 *   (0x8,A6)  event_type  word -> D0     (0xA,A6)  target_idx  word -> D1
 *   (0xC,A6)  signal      word -> D2     (0xE,A6)  param       long -> D3
 *   (0x12,A6) success     long (only its address and zero-ness are used)
 *   A6-0x18   the 0x12-byte audit record   A6-0x1A  success word (0/1)
 *
 * Callers: PROC2_$SIGNAL 0x00E3F098/0x00E3F122, PROC2_$SIGNAL_PGROUP
 * 0x00E3F230.
 *
 * Original address: 0x00e3e748
 */

#include "proc2/proc2_internal.h"

/*
 * 0x00E3E806 (`pea (0x22,PC)` at 0x00E3E7E2): bytes 00 0a -- the word 10,
 * AUDIT_$LOG_EVENT's data length (param + asid + signal + upid).
 */
static const uint16_t proc2_signal_audit_len_00e3e806 = 10;

/*
 * The record built at A6-0x18.  Stores: 0x00E3E76A (+0x00), 0x00E3E786
 * (+0x04), 0x00E3E78E (+0x08), 0x00E3E7AE/0x00E3E7C0 (+0x0C), 0x00E3E78A
 * (+0x0E), 0x00E3E7A8/0x00E3E7CA (+0x10).  The first eight bytes are the
 * event UID; the ten from +0x08 are the variable data.
 */
typedef struct signal_audit_event_t {
    uid_t       event_uid;      /* 0x00: high 0x4165836C, low (type << 24) + 0xFDED */
    uint32_t    param;          /* 0x08 */
    uint16_t    asid;           /* 0x0C: 0 for a process-group event */
    uint16_t    signal;         /* 0x0E */
    uint16_t    upid;           /* 0x10: process upid or group upgid */
} signal_audit_event_t;

_Static_assert(__builtin_offsetof(signal_audit_event_t, param) == 0x08, "signal_audit_event_t.param");
_Static_assert(__builtin_offsetof(signal_audit_event_t, asid) == 0x0C, "signal_audit_event_t.asid");
_Static_assert(__builtin_offsetof(signal_audit_event_t, signal) == 0x0E, "signal_audit_event_t.signal");
_Static_assert(__builtin_offsetof(signal_audit_event_t, upid) == 0x10, "signal_audit_event_t.upid");
#if defined(ARCH_M68K)
/* 18 bytes on the target; a 4-byte-aligning host pads the tail to 20. */
_Static_assert(sizeof(signal_audit_event_t) == 0x12, "signal_audit_event_t is 0x12 bytes");
#endif

void PROC2_$LOG_SIGNAL_EVENT(uint16_t event_type, int16_t target_idx,
                              uint16_t signal, uint32_t param, int32_t success)
{
    signal_audit_event_t event;     /* A6-0x18 */
    uint16_t success_flag;          /* A6-0x1A */
    proc2_info_t *entry;

    /* 0x00E3E760: tst.b AUDIT_$ENABLED / bpl -> exit */
    if (AUDIT_$ENABLED >= 0) {
        return;
    }

    /* 0x00E3E76A */
    event.event_uid.high = 0x4165836C;

    /* 0x00E3E772-0x00E3E786: (type & 0xFF) ext.l, swap, clr.w, lsl.l #8,
     * + 0xFDED = ((type & 0xFF) << 24) + 0xFDED */
    event.event_uid.low = ((uint32_t)(event_type & 0xFF) << 24) + 0xFDED;

    event.signal = signal;          /* 0x00E3E78A */
    event.param = param;            /* 0x00E3E78E */

    /* 0x00E3E792: cmpi.w #0x2 */
    if (event_type == 2) {
        /* 0x00E3E798-0x00E3E7AE: PGROUP_TABLE[idx].upgid (0x3F34,A1), asid 0 */
        event.upid = PGROUP_ENTRY(target_idx)->upgid;
        event.asid = 0;
    } else {
        /* 0x00E3E7B4-0x00E3E7CA: entry+0x96 and entry+0x16 */
        entry = P2_INFO_ENTRY(target_idx);
        event.asid = entry->asid;
        event.upid = entry->upid;
    }

    /* 0x00E3E7D0-0x00E3E7DC: tst.l (0x12,A6) -> 0 or 1 */
    success_flag = (success != 0) ? 1 : 0;

    /*
     * 0x00E3E7E2-0x00E3E7F6, pushes right to left:
     *   &len (0x00E3E806), &event.param, &success (0x12,A6), &success_flag,
     *   &event
     */
    AUDIT_$LOG_EVENT(&event.event_uid, &success_flag, (status_$t *)&success,
                     (char *)&event.param, &proc2_signal_audit_len_00e3e806);
}
