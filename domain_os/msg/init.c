/*
 * MSG_$INIT - Initialize MSG subsystem
 *
 * Initializes the message passing subsystem:
 * - Gets a data page for message buffers
 * - Initializes the exclusion lock
 * - Sets up initial socket ownership state
 *
 * Original address: 0x00E31B84
 * Original size: 144 bytes
 */

#include "msg/msg_internal.h"
#include "netbuf/netbuf.h"

void MSG_$INIT(void)
{
#if defined(ARCH_M68K)
    status_$t status;

    /*
     * Get a data page for network message handling.
     * NETBUF_$GET_DAT returns the physical address in DPAGE_PA.
     */
    NETBUF_$GET_DAT((uint32_t *)MSG_$DPAGE_PA);

    /*
     * Get the virtual address mapping for the data page.
     */
    NETBUF_$GETVA(*(uint32_t *)MSG_$DPAGE_PA, (uint32_t *)MSG_$DPAGE_VA, &status);
    if (status != status_$ok) {
        CRASH_SYSTEM(&status);
    }

    /*
     * Initialize the MSG socket exclusion lock.
     */
    ML_$EXCLUSION_INIT((void *)MSG_$SOCK_LOCK);

    /*
     * Initialize socket ownership entries for special sockets.
     * Sets up ownership patterns at base + 0x1E0, 0x1E8, 0x1F8, 0x200, 0x208.
     * Value 0x04000000 appears to be a special marker (possibly ASID 26 ownership).
     */
    {
        uint32_t *base = (uint32_t *)MSG_$DATA_BASE;

        /* Socket ownership entries initialization */
        base[0x1E0 / 4] = 0x04000000;  /* offset 0x1E0 */
        base[0x1E4 / 4] = 0;            /* offset 0x1E4 */
        base[0x1E8 / 4] = 0x04000000;  /* offset 0x1E8 */
        base[0x1EC / 4] = 0;            /* offset 0x1EC */
        base[0x1F8 / 4] = 0x04000000;  /* offset 0x1F8 */
        base[0x1FC / 4] = 0;            /* offset 0x1FC */
        base[0x200 / 4] = 0x04000000;  /* offset 0x200 */
        base[0x204 / 4] = 0;            /* offset 0x204 */
        base[0x208 / 4] = 0x04000000;  /* offset 0x208 */
        base[0x20C / 4] = 0;            /* offset 0x20C */
    }
#endif
}
