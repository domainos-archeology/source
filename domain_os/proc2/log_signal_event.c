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

/* Audit enabled flag */
#if defined(ARCH_M68K)
    #define AUDIT_ENABLED       (*(uint8_t*)0xE2E09E)
#else
    extern uint8_t audit_enabled;
    #define AUDIT_ENABLED       audit_enabled
#endif

/* Static data for audit event (from 0x00e3e806) */
static const uint8_t audit_event_extra[] = { 0 };

/*
 * Audit event header structure
 */
typedef struct signal_audit_event_t {
    uint32_t    event_code;     /* 0x4165836c = "Aesl" (Audit event signal) */
    uint32_t    event_subcode;  /* (event_type << 24) | 0xFDED */
    uint16_t    asid;           /* ASID of target process */
    uint16_t    signal;         /* Signal number */
    uint32_t    param;          /* Signal parameter */
    uint16_t    upid;           /* UPID of target process */
} signal_audit_event_t;

void PROC2_$LOG_SIGNAL_EVENT(uint16_t event_type, int16_t target_idx,
                              uint16_t signal, uint32_t param, int32_t success)
{
    signal_audit_event_t event;
    uint16_t success_flag;
    pgroup_entry_t *pg;
    proc2_info_t *entry;

    /* Only log if auditing is enabled (high bit set) */
    if ((int8_t)AUDIT_ENABLED >= 0) {
        return;
    }

    /* Build audit event header */
    event.event_code = 0x4165836C;  /* "Aesl" */
    event.event_subcode = ((event_type & 0xFF) << 24) | 0xFDED;
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

    /* Log the event */
    AUDIT_$LOG_EVENT(&event.event_code, &success_flag, &success,
                     &event.param, (void*)audit_event_extra);
}
