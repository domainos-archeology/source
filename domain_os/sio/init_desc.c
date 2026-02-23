/*
 * SIO_$INIT_DESC - Initialize an SIO descriptor
 *
 * Populates a full sio_desc_t structure with context, owner, parameter
 * block, handler function pointers, vtable entries, and transmit buffer
 * pointer. Also stores a back-pointer into the DTTE at offset 0x28
 * (tty_handler field), then calls SIO_$I_INIT to finalize.
 *
 * Copy operations:
 *   desc[0x00]       = *context_ptr          (context handle)
 *   desc[0x04]       = *owner_ptr            (owner handle)
 *   desc[0x4C..0x61] = param_block[0..21]    (22-byte param block, sio_params_t)
 *   desc[0x28..0x37] = handlers[0..3]        (4 handler ptrs: rcv, drain, dcd, special)
 *   desc[0x3C..0x4B] = vtable[0x14..0x23]    (4 vtable ptrs: output_char, set_params,
 *                                              inq_params, reserved_48)
 *   desc[0x24]       = *txbuf_ptr            (transmit buffer pointer)
 *   dtte[0x28]       = desc                  (back-pointer into DTTE tty_handler)
 *
 * Then calls SIO_$I_INIT(desc) to initialize state/event counts.
 *
 * Parameters:
 *   desc         - SIO descriptor to initialize (sio_desc_t *)
 *   param_block  - Source parameter block (22 bytes, sio_params_t + padding)
 *   dtte         - DTTE entry pointer (receives back-pointer at offset 0x28)
 *   owner_ptr    - Pointer to owner handle (dereferenced)
 *   txbuf_ptr    - Pointer to transmit buffer pointer (dereferenced)
 *   handlers     - Array of 4 handler function pointers
 *   context_ptr  - Pointer to context handle (dereferenced)
 *   vtable       - Vtable structure (entries copied from offset 0x14)
 *
 * Assembly register mapping:
 *   desc         = A2 (from A6+0x08)
 *   param_block  = A4 (from A6+0x0C)
 *   dtte         = A0 (from A6+0x10)
 *   owner_ptr    = A3 (from A6+0x14), dereferenced
 *   txbuf_ptr    = A3 (from A6+0x18), dereferenced
 *   handlers     = A1 (from A6+0x1C)
 *   context_ptr  = A1 (from A6+0x20), dereferenced
 *   vtable       = A1 (from A6+0x24)
 *
 * Original address: 0x00e32ab2
 * Size: 116 bytes
 */

#include "sio/sio_internal.h"

void SIO_$INIT_DESC(sio_desc_t *desc, void *param_block, void *dtte,
                    m68k_ptr_t *owner_ptr, m68k_ptr_t *txbuf_ptr,
                    m68k_ptr_t *handlers, m68k_ptr_t *context_ptr,
                    char *vtable)
{
    uint32_t *src;
    uint32_t *dst;
    int16_t i;

    /* Store context handle (from *context_ptr) at desc offset 0x00 */
    desc->context = *context_ptr;

    /* Store owner handle (from *owner_ptr) at desc offset 0x04 */
    desc->owner = *owner_ptr;

    /*
     * Copy 22-byte parameter block to desc offset 0x4C.
     * Loop copies 5 longwords (20 bytes) then 1 word (2 bytes) = 22 bytes.
     * This covers the full sio_params_t structure.
     */
    dst = (uint32_t *)&desc->params;
    src = (uint32_t *)param_block;
    for (i = 4; i >= 0; i--) {
        *dst++ = *src++;
    }
    *(uint16_t *)dst = *(uint16_t *)src;

    /*
     * Copy 4 handler function pointers to desc offsets 0x28-0x37.
     * These are: rcv_handler, drain_handler, dcd_handler, special_rcv.
     */
    desc->rcv_handler = handlers[0];
    desc->drain_handler = handlers[1];
    desc->dcd_handler = handlers[2];
    desc->special_rcv = handlers[3];

    /*
     * Copy 4 vtable entries from vtable+0x14 to desc offsets 0x3C-0x4B.
     * These are: output_char, set_params, inq_params, reserved_48.
     */
    src = (uint32_t *)(vtable + 0x14);
    desc->output_char = src[0];
    desc->set_params = src[1];
    desc->inq_params = src[2];
    desc->reserved_48 = src[3];

    /* Store transmit buffer pointer at desc offset 0x24 */
    desc->txbuf = *txbuf_ptr;

    /* Store back-pointer: desc pointer into DTTE at offset 0x28 (tty_handler) */
    ((dtte_t *)dtte)->tty_handler = (m68k_ptr_t)desc;

    /* Initialize SIO descriptor state and event counts */
    SIO_$I_INIT(desc);
}
