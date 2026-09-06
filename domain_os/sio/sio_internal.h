/*
 * SIO - Serial I/O Module (Internal)
 *
 * Internal definitions for the SIO subsystem.
 * Contains private functions and data structures.
 */

#ifndef SIO_INTERNAL_H
#define SIO_INTERNAL_H

#include "sio/sio.h"
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
 * SIO_DELAY_RESTART_QUEUE_ELEM - Queue element storage for delay restart
 *
 * Used by TIME_$Q_ADD_CALLBACK for transmit delays.
 *
 * Original address: 0x00e2dddc
 */
extern time_queue_elem_t SIO_DELAY_RESTART_QUEUE_ELEM;

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
 * Called by the time subsystem when a transmit delay expires.
 * Restarts transmission.
 *
 * Parameters:
 *   args - Pointer to array containing SIO descriptor pointer
 *
 * Returns:
 *   Result from SIO_$I_TSTART
 *
 * Original address: 0x00e1c690
 */
uint16_t SIO_DELAY_RESTART(sio_desc_t **args);

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
 * SIO_$INIT_DESC - Initialize an SIO descriptor
 *
 * Populates a full sio_desc_t structure with context, owner, parameter
 * block, handler function pointers, vtable entries, and transmit buffer
 * pointer. Stores a back-pointer into the DTTE, then calls SIO_$I_INIT.
 *
 * Parameters:
 *   desc         - SIO descriptor to initialize
 *   param_block  - Source parameter block (22 bytes)
 *   dtte         - DTTE entry pointer (receives back-pointer at offset 0x28)
 *   owner_ptr    - Pointer to owner handle (dereferenced)
 *   txbuf_ptr    - Pointer to transmit buffer pointer (dereferenced)
 *   handlers     - Array of 4 handler function pointers
 *   context_ptr  - Pointer to context handle (dereferenced)
 *   vtable       - Vtable structure (entries copied from offset 0x14)
 *
 * Original address: 0x00e32ab2
 */
void SIO_$INIT_DESC(sio_desc_t *desc, void *param_block, void *dtte,
                    m68k_ptr_t *owner_ptr, m68k_ptr_t *txbuf_ptr,
                    m68k_ptr_t *handlers, m68k_ptr_t *context_ptr,
                    char *vtable);

/*
 * SIO_$INIT_DTTE - Initialize a DTTE (Display Terminal Table Entry)
 *
 * Initializes three inline event counts at offsets 0x00, 0x0C, and 0x18,
 * sets the discipline field, and clears the flags byte.
 *
 * Parameters:
 *   dtte       - Pointer to DTTE entry to initialize
 *   discipline - Terminal discipline value (0=TTY, 2=console, etc.)
 *
 * Original address: 0x00e32b76
 */
void SIO_$INIT_DTTE(dtte_t *dtte, int16_t discipline);

/*
 * SIO_$INIT_DRAIN_HANDLER - Initialize output buffer drain handler
 *
 * Sets up a 3-word handler record: function pointer, context, and data.
 * The function pointer is set to TTY_$I_OUTPUT_BUFFER_DRAINED.
 *
 * Parameters:
 *   handler     - Pointer to 3-word handler record
 *   dtte        - DTTE pointer (unused, part of calling convention)
 *   data_ptr    - Pointer to data pointer value (stored at handler[2])
 *   context_ptr - Pointer to context pointer value (stored at handler[1])
 *
 * Original address: 0x00e32bb8
 */
void SIO_$INIT_DRAIN_HANDLER(m68k_ptr_t *handler, void *dtte,
                              m68k_ptr_t *data_ptr, m68k_ptr_t *context_ptr);

/*
 * SIO_$INIT_LINE - Initialize SIO TTY line descriptor
 *
 * Sets up a TTY descriptor from SIO port configuration data. Copies the
 * line identifier (from *config), hardware register addresses (from hw_info),
 * I/O buffer pointers (from port_data), then calls TTY_$I_INIT.
 *
 * Parameters:
 *   desc      - TTY descriptor to initialize
 *   port_data - SIO port data structure (DTTE)
 *   config    - Pointer to m68k_ptr_t value used as line ID (dereferenced once)
 *   hw_info   - Hardware info block (register addresses at +0x08)
 *
 * Original address: 0x00e32b26
 */
void SIO_$INIT_LINE(void *desc, void *port_data, m68k_ptr_t *config, void *hw_info);

/*
 * OS_TERM_INIT - Initialize an OS terminal structure (console only)
 *
 * Sets up a console terminal data structure with handler function pointers,
 * SIO descriptor reference, line data reference, and DTTE pointer.
 * Then stores a back-pointer into the DTTE at offset 0x2c (alt_handler)
 * and calls KBD_$INIT.
 *
 * Layout written to param_1:
 *   [0x00] = *param_4            (receive handler, e.g., TTY_$I_RCV)
 *   [0x04] = *(param_6 + 4)     (transmit start handler)
 *   [0x08] = *(param_6 + 8)     (reserved handler)
 *   [0x0C] = *(param_6 + 0xc)   (reserved handler)
 *   [0x10] = *param_5            (SIO descriptor address)
 *   [0x14] = *param_3            (line data address)
 *   [0x48] = param_2             (DTTE pointer)
 *
 * Parameters:
 *   term_data    - Console terminal data structure to initialize
 *   dtte         - DTTE entry pointer (stored at term_data+0x48, gets backref at +0x2c)
 *   line_data_pp - Pointer to line data address (dereferenced)
 *   i_rcv_ptr    - Pointer to receive handler pointer (dereferenced)
 *   sio_desc_pp  - Pointer to SIO descriptor address (dereferenced)
 *   vtable       - Init vtable (handlers at offsets +4, +8, +0xc)
 *
 * Original address: 0x00e32a60
 */
void OS_TERM_INIT(void *term_data, void *dtte, m68k_ptr_t *line_data_pp,
                  m68k_ptr_t *i_rcv_ptr, m68k_ptr_t *sio_desc_pp, void *vtable);

/*
 * ============================================================================
 * External References from Other Modules
 * ============================================================================
 */

/*
 * From TERM module
 * TERM_$MAX_DTTE is provided as a macro in term/term.h aliasing TERM_$DATA.max_dtte.
 */
extern dtte_t DTTE[];

/*
 * TERM_$GET_REAL_LINE - Map virtual line to real line
 *
 * Original address: 0x00e1a9d4
 */
extern int16_t TERM_$GET_REAL_LINE(int16_t line_num, status_$t *status_ret);

/*
 * FIM_$QUIT_EC / FIM_$QUIT_VALUE come from fim/fim.h,
 * PROC1_$AS_ID comes from proc1/proc1.h.
 */

/*
 * From TTY module
 */

/* TTY_$I_ENABLE_CRASH_FUNC - declared in tty/tty.h */

#endif /* SIO_INTERNAL_H */
