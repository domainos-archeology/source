/*
 * AST_$FETCH_PMAP_PAGE - Fetch a page's physical map data from network
 *
 * Used for remote objects to fetch physical page mapping information
 * from a network partner. Allocates a temporary page, reads the data
 * via network, copies to the output buffer, then frees the page.
 *
 * Parameters:
 *   uid_info - Pointer to UID info for the remote object
 *   output_buf - Output buffer for page map data (1KB = 256 uint32_t)
 *   flags - Network operation flags
 *   status - Status return
 *
 * Original address: 0x00e041a8
 */

#include "ast/ast_internal.h"
#include "mmu/mmu.h"
#include "mmap/mmap.h"
#include "netbuf/netbuf.h"
#include "network/network.h"
#include "area/area.h"

void AST_$FETCH_PMAP_PAGE(void *uid_info, uint32_t *output_buf,
                          uint16_t flags, status_$t *status)
{
    uint32_t ppn_array[32];
    uint8_t temp_buf[8];
    uint32_t temp_addr;
    int i;

    ML_$LOCK(PMAP_LOCK_ID);

    /* Allocate a page */
    FUN_00e00d46(0x10001, ppn_array);

    ML_$UNLOCK(PMAP_LOCK_ID);

    /* Return the page data buffer */
    NETBUF_$RTN_DAT(ppn_array[0] << 10);

    /* Read from network */
    NETWORK_$READ_AHEAD(&AREA_$PARTNER, uid_info, ppn_array, flags, 1, 0, 0,
                        temp_buf, temp_buf, temp_buf, status);

    if (*status == status_$ok) {
        ML_$LOCK(PMAP_LOCK_ID);

        /* Install the page temporarily to copy data */
        MMU_$INSTALL(ppn_array[0], (uint32_t)AST_$ZERO_BUFF, 0x16);

        /* Copy 1KB of data (256 uint32_t) */
        uint32_t *src = AST_$ZERO_BUFF;
        for (i = 0; i < 256; i++) {
            output_buf[i] = src[i];
        }

        /* Remove and free the page */
        MMU_$REMOVE(ppn_array[0]);
        MMAP_$FREE(ppn_array[0]);

        ML_$UNLOCK(PMAP_LOCK_ID);
    } else {
        /* Error - get and free the buffer */
        NETBUF_$GET_DAT(&temp_addr);
        MMAP_$FREE(temp_addr >> 10);
    }
}
