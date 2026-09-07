/*
 * PROC2_$LOG_SIGNAL_EVENT - Log a signal event to audit subsystem
 *
 * Called to log signal delivery events for auditing purposes.
 * Only logs if auditing is enabled.
 *
 * Parameters:
 *   event_type  - Type of signal event (2 = process group, other = process)
 *   target_idx  - Target process/pgroup index
 *   signal      - Signal number
 *   param       - Signal parameter
 *   success     - Non-zero if signal was delivered successfully
 *
 * Original address: 0x00e3e748
 */

#include "proc2/proc2_internal.h"

/* AUDIT_$ENABLED (0xE2E09E) - audit/audit.h */

/*
 * 0x00E3E806: the word constant 10, passed by reference as
 * AUDIT_$LOG_EVENT's data length (0x00E3E7E2 `pea (0x22,PC)`).
 * Ten bytes = param + asid + signal + upid.
 */
static const uint16_t audit_data_len = 10;

/*
 * The 0x12-byte record PROC2_$LOG_SIGNAL_EVENT builds at (-0x18,A6).
 * Field offsets are exactly the stores at 0x00E3E76A (+0x00), 0x00E3E786
 * (+0x04), 0x00E3E78E (+0x08), 0x00E3E7AE/0x00E3E7C0 (+0x0C),
 * 0x00E3E78A (+0x0E) and 0x00E3E7A8/0x00E3E7CA (+0x10).
 *
 * The first eight bytes are the event UID AUDIT_$LOG_EVENT is handed;
 * the ten bytes from +0x08 on are its variable-length data.
 */
typedef struct signal_audit_event_t {
    uid_t       event_uid;      /* 0x00: high = 0x4165836C ("Ae\x83l"),
                                 *       low  = (event_type << 24) | 0xFDED */
    uint32_t    param;          /* 0x08: Signal parameter */
    uint16_t    asid;           /* 0x0C: ASID of target process */
    uint16_t    signal;         /* 0x0E: Signal number */
    uint16_t    upid;           /* 0x10: UPID of target process */
} signal_audit_event_t;

void PROC2_$LOG_SIGNAL_EVENT(uint16_t event_type, int16_t target_idx,
                              uint16_t signal, uint32_t param, int32_t success)
{
    signal_audit_event_t event;
    uint16_t success_flag;
    pgroup_entry_t *pg;
    proc2_info_t *entry;

    /* Only log if auditing is enabled (high bit set) */
    if (AUDIT_$ENABLED >= 0) {
        return;
    }

    /* Build audit event header */
    event.event_uid.high = 0x4165836C;
    /* 0x00E3E772-0x00E3E780: (event_type & 0xFF) is sign-extended, swapped,
     * the low word cleared and the whole shifted left by 8, then 0xFDED is
     * added -- i.e. ((event_type & 0xFF) << 24) + 0xFDED. */
    event.event_uid.low = ((uint32_t)(event_type & 0xFF) << 24) + 0xFDED;
    event.signal = signal;
    event.param = param;

    if (event_type == 2) {
        /* Process group event - get UPGID from pgroup table */
        pg = PGROUP_ENTRY(target_idx);
        event.upid = pg->upgid;
        event.asid = 0;
    } else {
        /* Process event - get ASID and UPID from process entry */
        entry = P2_INFO_ENTRY(target_idx);
        event.asid = entry->asid;
        event.upid = entry->upid;
    }

    /* Set success flag (0 or 1) */
    success_flag = (success != 0) ? 1 : 0;

    /*
     * 0x00E3E7E2-0x00E3E7F6, right to left: &audit_data_len, &event.param
     * (the record at -0x10), &success (the caller's longword at (0x12,A6)),
     * &success_flag, &event.
     */
    AUDIT_$LOG_EVENT(&event.event_uid, &success_flag, (uint32_t *)&success,
                     (char *)&event.param, &audit_data_len);
}
