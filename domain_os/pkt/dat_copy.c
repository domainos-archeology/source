/*
 * PKT_$DAT_COPY - Copy data from network buffers
 *
 * Copies data from network buffer pages to a destination virtual address.
 * Each buffer holds up to 0x400 (1024) bytes.
 *
 * The assembly shows:
 * 1. Loop while remaining length > 0
 * 2. Determine chunk size (min of remaining and 0x400)
 * 3. Get virtual address for buffer via NETBUF_$GETVA
 * 4. Copy data using OS_$DATA_COPY
 * 5. Return virtual address via NETBUF_$RTNVA
 * 6. Advance destination pointer and buffer array
 *
 * Original address: 0x00E12834
 */

#include "pkt/pkt_internal.h"
#include "misc/crash_system.h"

void PKT_$DAT_COPY(uint32_t *buffers, int16_t len, char *dest_va)
{
    int16_t chunk_size;
    int16_t remaining;
    char *buf_va;
    status_$t status;

    remaining = len;

    while (remaining > 0) {
        /* Determine chunk size - max 0x400 bytes per buffer */
        chunk_size = remaining;
        if (chunk_size > PKT_CHUNK_SIZE) {
            chunk_size = PKT_CHUNK_SIZE;
        }

        /* Get virtual address for this buffer */
        NETBUF_$GETVA(*buffers, (uint32_t *)&buf_va, &status);
        if (status != status_$ok) {
            CRASH_SYSTEM(&status);
        }

        /* Copy data from buffer to destination */
        OS_$DATA_COPY(buf_va, dest_va, (uint32_t)chunk_size);

        /* Return the virtual address mapping */
        NETBUF_$RTNVA((uint32_t *)&buf_va);

        /* Advance to next buffer and update remaining */
        remaining -= chunk_size;
        dest_va += chunk_size;
        buffers++;
    }
}
