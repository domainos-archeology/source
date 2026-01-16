/*
 * PKT_$COPY_TO_PA - Copy data to physical address buffers
 *
 * Copies data from a virtual address to network buffer pages.
 * Allocates necessary buffer pages and maps them.
 * Uses FIM cleanup handlers to ensure buffers are returned on error.
 *
 * The assembly shows:
 * 1. Set up FIM cleanup handler
 * 2. Loop while remaining length > 0
 * 3. Allocate buffer via NETBUF_$GET_DAT
 * 4. Get virtual address via NETBUF_$GETVA
 * 5. Copy data using OS_$DATA_COPY
 * 6. Return virtual address via NETBUF_$RTNVA
 * 7. Advance pointers
 * 8. Release cleanup handler on success
 *
 * Original address: 0x00E1251C
 */

#include "pkt/pkt_internal.h"
#include "misc/crash_system.h"

void PKT_$COPY_TO_PA(char *src_va, uint16_t len, uint32_t *buffers_out,
                     status_$t *status_ret)
{
    uint16_t chunk_size;
    int16_t remaining;
    char *buf_va;
    char *src_ptr;
    uint32_t *buf_ptr;
    int16_t buf_count;
    status_$t status;
    uint8_t cleanup_context[24];  /* FIM cleanup context */

    *buffers_out = 0;
    buf_va = NULL;
    src_ptr = src_va;

    /* Set up cleanup handler */
    status = FIM_$CLEANUP(cleanup_context);

    if (status == status_$cleanup_handler_set) {
        /* Normal path - cleanup handler is now set */
        remaining = (int16_t)len;
        buf_ptr = buffers_out;
        buf_count = 0;

        while (remaining > 0) {
            buf_count++;
            buf_ptr++;

            /* Allocate a buffer */
            NETBUF_$GET_DAT(buf_ptr - 1);

            /* Get virtual address for the buffer */
            NETBUF_$GETVA(*(buf_ptr - 1), (uint32_t *)&buf_va, &status);
            if (status != status_$ok) {
                CRASH_SYSTEM(&status);
                break;
            }

            /* Determine chunk size */
            chunk_size = PKT_CHUNK_SIZE;
            if ((int16_t)remaining < PKT_CHUNK_SIZE) {
                chunk_size = remaining;
            }

            /* Copy data to buffer */
            OS_$DATA_COPY(src_ptr, buf_va, (uint32_t)chunk_size);

            /* Return virtual address mapping */
            NETBUF_$RTNVA((uint32_t *)&buf_va);
            buf_va = NULL;

            /* Advance source pointer */
            src_ptr += PKT_CHUNK_SIZE;
            remaining -= PKT_CHUNK_SIZE;
        }

        /* Release cleanup handler */
        FIM_$RLS_CLEANUP(cleanup_context);
        *status_ret = status_$ok;
    } else {
        /* Cleanup path - error occurred or cleanup was triggered */
        if (buf_va != NULL) {
            NETBUF_$RTNVA((uint32_t *)&buf_va);
        }

        /* Signal the fault */
        FIM_$SIGNAL(status);
        *status_ret = status;
    }
}
