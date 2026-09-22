/*
 * TTY_$I_SIGNAL, TTY_$I_INTERRUPT, TTY_$I_HUP, TTY_$I_DXM_SIGNAL
 *
 * TTY_$I_SIGNAL, 0x00E1B824..0x00E1B8AA (136 bytes):
 *   0x00E1B82C  signal = word (0xc,A6); 3 -> 0, 2 -> 1, 0x15 -> 2, 1 -> 3,
 *               0x1A -> 4, 0x16 -> 5, anything else -> return (0x00E1B856)
 *   0x00E1B86E  pea (-0x4,A6)            status
 *               st -(SP)                 check_dup = true
 *               move.w #0xc,-(SP)        data_size = 12
 *   0x00E1B878  idx*4 + idx*8 = idx*12; (-0xc,A6) = &tty->signals[idx]
 *   0x00E1B88E  pea (-0xc,A6)            data = the local holding that address
 *               pea (0x18,PC)            &PTR_TTY_$I_DXM_SIGNAL (0x00E1B8AC)
 *               move.l #0xe2adc4,-(SP)   &DXM_$UNWIRED_Q
 *               jsr DXM_$ADD_CALLBACK (0x00E16FE0)
 *   The 12-byte datum copies the entry pointer plus the eight bytes above it
 *   in the frame; only the pointer is read back by TTY_$I_DXM_SIGNAL.
 *
 * TTY_$I_INTERRUPT, 0x00E1BEA8..0x00E1BECC (38 bytes):
 *   TTY_$I_FLUSH_INPUT(tty); TTY_$I_SIGNAL(tty, 2)
 *
 * TTY_$I_HUP, 0x00E1BECE..0x00E1BF0C (64 bytes):
 *   clr.w (0x54,A2) session_id = 0; TTY_$I_FLUSH_INPUT(tty);
 *   TTY_$I_FLUSH_OUTPUT(tty); TTY_$I_SIGNAL(tty, 1); TTY_$I_SIGNAL(tty, 0x16)
 *
 * TTY_$I_DXM_SIGNAL, 0x00E671DC..0x00E6720C (50 bytes), the DXM callback:
 *   0x00E671E4  A3 = *(param) = the signal entry; A2 = entry->tty_desc
 *   0x00E671EE  PROC2_$SIGNAL_PGROUP_OS(&tty->pgroup_uid (0x4c,A2),
 *               &entry->signal_num (0x8,A3), &entry->fault_status (0x4,A3),
 *               &local status (-0x4,A6))   [jsr 0x00E3F2C2]
 */

#include "tty/tty_internal.h"
#include "dxm/dxm.h"
#include "proc2/proc2.h"

void TTY_$I_SIGNAL(tty_desc_t *tty, short signal)
{
    short signal_index;                         /* D2w */
    tty_signal_entry_t *entry_ptr;              /* (-0xc,A6) */
    status_$t status;                           /* (-0x4,A6) */

    switch (signal) {                           /* 0x00E1B832..0x00E1B856 */
        case TTY_SIG_QUIT:   signal_index = 0; break;   /* 0x03 */
        case TTY_SIG_INT:    signal_index = 1; break;   /* 0x02 */
        case TTY_SIG_TSTP:   signal_index = 2; break;   /* 0x15 */
        case TTY_SIG_HUP:    signal_index = 3; break;   /* 0x01 */
        case TTY_SIG_WINCH:  signal_index = 4; break;   /* 0x1A */
        case TTY_SIG_CONT:   signal_index = 5; break;   /* 0x16 */
        default:
            return;
    }

    entry_ptr = &tty->signals[signal_index];    /* 0x00E1B878..0x00E1B88A */

    DXM_$ADD_CALLBACK(&DXM_$UNWIRED_Q, &PTR_TTY_$I_DXM_SIGNAL,
                      (void **)&entry_ptr, 12, true, &status);   /* 0x00E1B86E..0x00E1B89C */
}

void TTY_$I_INTERRUPT(tty_desc_t *tty)
{
    TTY_$I_FLUSH_INPUT(tty);                    /* 0x00E1BEB4 */
    TTY_$I_SIGNAL(tty, TTY_SIG_INT);            /* 0x00E1BEBC..0x00E1BEC2 */
}

void TTY_$I_HUP(tty_desc_t *tty)
{
    tty->session_id = 0;                        /* 0x00E1BED8 */
    TTY_$I_FLUSH_INPUT(tty);                    /* 0x00E1BEDE */
    TTY_$I_FLUSH_OUTPUT(tty);                   /* 0x00E1BEE6 */
    TTY_$I_SIGNAL(tty, TTY_SIG_HUP);            /* 0x00E1BEEE..0x00E1BEF4 */
    TTY_$I_SIGNAL(tty, TTY_SIG_CONT);           /* 0x00E1BEFC..0x00E1BF02 */
}

void TTY_$I_DXM_SIGNAL(tty_signal_entry_t **entry_ptr_ptr)
{
    tty_signal_entry_t *entry;                  /* A3 */
    tty_desc_t *tty;                            /* A2 */
    status_$t status;                           /* (-0x4,A6) */

    entry = *entry_ptr_ptr;                                     /* 0x00E671E4 */
    tty = (tty_desc_t *)ARCH_VA_TO_PTR(entry->tty_desc);        /* 0x00E671EC */

    PROC2_$SIGNAL_PGROUP_OS(&tty->pgroup_uid, (int16_t *)&entry->signal_num,
                            (uint32_t *)&entry->fault_status, &status);   /* 0x00E671EE..0x00E671FE */
}
