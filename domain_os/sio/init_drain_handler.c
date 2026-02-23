/*
 * SIO_$INIT_DRAIN_HANDLER - Initialize output buffer drain handler
 *
 * Sets up a 3-word (12-byte) handler record for output buffer drain
 * notification. When the output buffer is drained, the system calls
 * through handler[0] with handler[1] as context.
 *
 * Handler structure layout:
 *   [0] = TTY_$I_OUTPUT_BUFFER_DRAINED function pointer (m68k_ptr_t)
 *   [1] = Context pointer (from *context_ptr)
 *   [2] = Data pointer (from *data_ptr)
 *
 * Parameters:
 *   handler     - Pointer to 3-word handler record to initialize
 *   dtte        - DTTE pointer (unused directly, part of calling convention)
 *   data_ptr    - Pointer to data pointer value (stored at handler[2])
 *   context_ptr - Pointer to context pointer value (stored at handler[1])
 *
 * Assembly register mapping:
 *   handler     = A0 (from A6+0x08)
 *   dtte        = (A6+0x0C, not accessed)
 *   data_ptr    = A1 (from A6+0x10), dereferenced
 *   context_ptr = A2 (from A6+0x14), dereferenced
 *
 * Original address: 0x00e32bb8
 * Size: 40 bytes
 */

#include "sio/sio_internal.h"

void SIO_$INIT_DRAIN_HANDLER(m68k_ptr_t *handler, void *dtte,
                              m68k_ptr_t *data_ptr, m68k_ptr_t *context_ptr)
{
    /* Store data pointer at handler[2] */
    handler[2] = *data_ptr;

    /* Store TTY_$I_OUTPUT_BUFFER_DRAINED function pointer at handler[0] */
    handler[0] = (m68k_ptr_t)TTY_$I_OUTPUT_BUFFER_DRAINED;

    /* Store context pointer at handler[1] */
    handler[1] = *context_ptr;
}
