/*
 * SIO - Serial I/O Module (Internal)
 *
 * Internal definitions for the SIO subsystem.
 * Contains private functions and data structures.
 */

#ifndef SIO_INTERNAL_H
#define SIO_INTERNAL_H

#include "ml/ml.h"
#include "sio/sio.h"
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
 * From FIM module
 */
extern ec_$eventcount_t FIM_$QUIT_EC[];
extern int32_t FIM_$QUIT_VALUE[];

/*
 * From PROC1 module
 */
extern int16_t PROC1_$AS_ID;

/*
 * From TTY module
 */

/* TTY_$I_ENABLE_CRASH_FUNC - declared in tty/tty.h */

#endif /* SIO_INTERNAL_H */
