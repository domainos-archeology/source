/*
 * SIO - Serial I/O Module (Internal)
 *
 * Internal definitions for the SIO subsystem.
 * Contains private functions and data structures.
 */

#ifndef SIO_INTERNAL_H
#define SIO_INTERNAL_H

#include "sio/sio.h"
#include "os/os.h"      /* OS_TERM_INIT */
#include "fim/fim.h"
#include "ml/ml.h"
#include "proc1/proc1.h"
#include "term/term.h"
#include "tty/tty.h"
#include "time/time.h"
#include "math/math.h"

/*
 * ============================================================================
 * Internal Data
 * ============================================================================
 */

/*
 * SIO_$SPIN_LOCK - Spin lock for SIO operations
 *
 * Used to protect SIO error handling operations.
 *
 * Original address: 0x00e82458
 */
extern uint32_t SIO_$SPIN_LOCK;

/*
 * There is no global delay queue element.  0x00E1C8C4 "pea (0x8,A0)" hands
 * TIME_$Q_ADD_CALLBACK the DESCRIPTOR's own element (sio_desc_t.delay_qelem),
 * and 0x00E2DDDC - which an earlier decompilation named
 * SIO_DELAY_RESTART_QUEUE_ELEM - is the SIO module data base, whose first six
 * bytes are the zero interval "pea (A5)" passes at 0x00E1C8C8.
 */

/*
 * ============================================================================
 * TERM_$DATA Layout Constants
 *
 * The SIO initialization code computes addresses relative to TERM_$DATA.
 * These constants capture the offsets used by SIO_$INIT.
 * ============================================================================
 */

/* Generic port configuration template offsets (within TERM_$DATA) */
#define SIO_GENERIC_PARAM_OFFSET    0x00    /* Generic sio_params_t template */
#define SIO_GENERIC_HANDLER_OFFSET  0x18    /* Generic handler array (TTY_$I_RCV, ...) */
#define SIO_GENERIC_HW_INFO_OFFSET  0x40    /* Generic hardware info block */

/* Console port configuration template offsets (within TERM_$DATA) */
#define SIO_CONSOLE_PARAM_OFFSET    0x58    /* Console sio_params_t template */
#define SIO_CONSOLE_HW_INFO_OFFSET  0x70    /* Console hardware info block */
#define SIO_CONSOLE_HANDLER_OFFSET  0x88    /* Console handler array (KBD_$RCV, ...) */
#define SIO_CONSOLE_VTABLE_OFFSET   0xb0    /* Console init vtable (for OS_TERM_INIT) */
#define SIO_CONSOLE_I_RCV_OFFSET    0xc0    /* Console PTR_TTY_$I_RCV */

/* Per-port SIO descriptor array */
#define SIO_DESC_BASE_OFFSET        0xf78   /* Start of SIO descriptor array */
#define SIO_DESC_STRIDE             0x78    /* Bytes per SIO descriptor (120) */

/* Per-line TTY data */
#define SIO_LINE_DATA_STRIDE        0x4dc   /* Bytes per line data block (1244) */
#define SIO_LINE_DATA_ADJUST        0x384   /* Subtract from line base for TTY desc */
#define SIO_LINE_TXBUF_OFFSET       0x4e    /* Offset from line base to txbuf area */

/* Console-specific per-port data (indexed by port * stride + offset) */
#define SIO_CONSOLE_PORT_STRIDE     0xe4    /* Console per-port data stride (228) */
#define SIO_CONSOLE_TERM_OFFSET     0x1084  /* Console terminal data start */
#define SIO_CONSOLE_TXBUF_OFFSET    0x1122  /* Console txbuf start */
#define SIO_DRAIN_HANDLER_STRIDE    0x0c    /* Drain handler per-port stride (12) */
#define SIO_DRAIN_HANDLER_OFFSET    0x114c  /* Drain handler record start */

/* Misc */
#define SIO_SET_PARAMS_ALL_MASK     0x3fff  /* All parameter change bits set */

/*
 * ============================================================================
 * Internal Function Declarations
 * ============================================================================
 */

/*
 * SIO_DELAY_RESTART - Callback for transmit delay completion
 *
 * Called by the time subsystem when a transmit delay expires; clears the
 * delay-active and transmit-active bits and restarts transmission.
 *
 * Parameters:
 *   arg - the standard two-level TIME callback argument (see
 *         time_$callback_arg_t): **arg is the descriptor's virtual address
 *
 * A Pascal procedure - 0x00E1C908 calls it with no result slot.
 *
 * Original address: 0x00e1c690
 */
void SIO_DELAY_RESTART(time_$callback_arg_t arg);

/*
 * sio_$set_break - Set or clear break state on serial line
 *
 * Under spin lock, modifies the line status flags at offset 0x75:
 *   enable < 0: clear bit 0, set bit 3 (break active)
 *   enable >= 0: clear bit 3 (break inactive)
 * Then calls through the output_start vtable entry (offset 0x48).
 *
 * Parameters:
 *   desc   - SIO descriptor
 *   enable - Negative to enable break, non-negative to disable
 *
 * Original address: 0x00e67e86
 */
void sio_$set_break(sio_desc_t *desc, uint8_t enable);

/*
 * SIO_$INIT_DESC / SIO_$INIT_DTTE / SIO_$INIT_DRAIN_HANDLER / SIO_$INIT_LINE
 * are declared in sio/sio.h (public: TERM_$INIT in term/ calls them).
 * OS_TERM_INIT is declared in os/os.h.
 */

/*
 * ============================================================================
 * External References from Other Modules
 * ============================================================================
 */

/*
 * From TERM module
 * TERM_$MAX_DTTE is provided as a macro in term/term.h aliasing TERM_$DATA.max_dtte.
 */
/* DTTE is TERM_$DATA.dtte; the alias lives in term/term.h. */

/* TERM_$GET_REAL_LINE (0x00e1a9d4) is declared in term/term.h */

/*
 * FIM_$QUIT_EC / FIM_$QUIT_VALUE come from fim/fim.h,
 * PROC1_$AS_ID comes from proc1/proc1.h.
 */

/*
 * From TTY module
 */

/* TTY_$I_ENABLE_CRASH_FUNC - declared in tty/tty.h */

#endif /* SIO_INTERNAL_H */
