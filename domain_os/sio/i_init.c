/*
 * SIO_$I_INIT - Initialize SIO descriptor fields
 *
 * Initializes the state fields and event count in an SIO descriptor.
 * Called during SIO_$INIT to prepare a descriptor for use.
 *
 * Clears:
 *   - state (offset 0x74)
 *   - pending_int (offset 0x64)
 *   - reserved field at offset 0x50
 *
 * Initializes:
 *   - Event count (offset 0x68)
 *
 * Original address: 0x00e67e5e
 */

#include "sio/sio_internal.h"

/* Accessor for reserved field at offset 0x50 */
#define SIO_DESC_RESERVED_50(desc) (*(uint32_t *)((char *)(desc) + 0x50))

void SIO_$I_INIT(sio_desc_t *desc)
{
    /* Clear state flags */
    desc->state = 0;

    /* Clear pending interrupts */
    desc->pending_int = 0;

    /* Clear reserved field */
    SIO_DESC_RESERVED_50(desc) = 0;

    /* Initialize event count */
    EC_$INIT(&desc->ec);
}
