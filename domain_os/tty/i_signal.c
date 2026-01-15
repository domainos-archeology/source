// TTY_$I_SIGNAL - Send a signal to the TTY's process group
// Address: 0x00e1b824
// Size: 136 bytes
//
// TTY_$I_INTERRUPT - Handle interrupt (^C)
// Address: 0x00e1bea8
// Size: 38 bytes
//
// TTY_$I_HUP - Handle hangup
// Address: 0x00e1bece
// Size: 64 bytes
//
// TTY_$I_DXM_SIGNAL - DXM callback for signal delivery
// Address: 0x00e671dc
// Size: 50 bytes

#include "tty/tty_internal.h"
#include "dxm/dxm.h"
#include "proc2/proc2.h"

void TTY_$I_SIGNAL(tty_desc_t *tty, short signal)
{
    short signal_index;
    tty_signal_entry_t *entry_ptr;
    status_$t status;

    // Map signal number to entry index
    switch (signal) {
        case TTY_SIG_QUIT:   // 0x03
            signal_index = 0;
            break;
        case TTY_SIG_INT:    // 0x02
            signal_index = 1;
            break;
        case TTY_SIG_TSTP:   // 0x15
            signal_index = 2;
            break;
        case TTY_SIG_HUP:    // 0x01
            signal_index = 3;
            break;
        case TTY_SIG_WINCH:  // 0x1A
            signal_index = 4;
            break;
        case TTY_SIG_CONT:   // 0x16
            signal_index = 5;
            break;
        default:
            return;  // Unknown signal, ignore
    }

    // Get pointer to the signal entry (each entry is 12 bytes)
    entry_ptr = &tty->signals[signal_index];

    // Queue the signal delivery via DXM callback
    // Options: 0x0C (size), 0xFF (flags)
    DXM_$ADD_CALLBACK(&DXM_$UNWIRED_Q, &PTR_TTY_$I_DXM_SIGNAL,
                      &entry_ptr, 0x0CFF00, &status);
}

void TTY_$I_INTERRUPT(tty_desc_t *tty)
{
    // Flush input buffer first
    TTY_$I_FLUSH_INPUT(tty);

    // Send interrupt signal
    TTY_$I_SIGNAL(tty, TTY_SIG_INT);
}

void TTY_$I_HUP(tty_desc_t *tty)
{
    // Clear session ID
    tty->session_id = 0;

    // Flush both buffers
    TTY_$I_FLUSH_INPUT(tty);
    TTY_$I_FLUSH_OUTPUT(tty);

    // Send hangup and continue signals
    TTY_$I_SIGNAL(tty, TTY_SIG_HUP);
    TTY_$I_SIGNAL(tty, TTY_SIG_CONT);
}

void TTY_$I_DXM_SIGNAL(tty_signal_entry_t **entry_ptr_ptr)
{
    tty_signal_entry_t *entry;
    tty_desc_t *tty;
    status_$t status;

    // Dereference to get actual entry
    entry = *entry_ptr_ptr;
    tty = (tty_desc_t *)(uintptr_t)entry->tty_desc;

    // Signal the process group
    // pgroup_uid is at offset 0x4C in tty_desc
    // signal_num is at offset 0x08 in entry
    // callback is at offset 0x04 in entry
    PROC2_$SIGNAL_PGROUP_OS(&tty->pgroup_uid,
                            &entry->signal_num,
                            &entry->callback,
                            &status);
}
