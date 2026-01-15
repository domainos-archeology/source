/*
 * MST_$FIND - Find physical address for virtual address
 *
 * This function looks up the physical address for a given virtual address.
 * If the page is already mapped, it returns the physical address directly.
 * If not mapped, it calls MST_$TOUCH to fault in the page.
 *
 * The flags parameter controls behavior:
 * - Bit 0: Must be 0 (assertion check)
 * - Bit 1: Wire the page after finding
 * - Bit 2: Must be 0 (assertion check)
 *
 * Bits 0 and 2 being set causes a system crash, indicating this function
 * should not be called with those flags.
 */

#include "mst_internal.h"
#include "misc/misc.h"

/*
 * MST_$FIND - Find physical address for virtual address
 *
 * @param virt_addr  Virtual address to look up
 * @param flags      Control flags (bit 1 = wire page)
 * @return Physical address, or result from MST_$TOUCH if not mapped
 */
uint32_t MST_$FIND(uint32_t virt_addr, uint16_t flags)
{
    uint32_t phys_addr;
    status_$t status[2];

    /*
     * Check for invalid flags - bits 0 and 2 must be clear.
     * This catches programming errors where caller passes wrong flags.
     */
    if ((flags & 5) != 0) {
        CRASH_SYSTEM(&MST_Ref_OutOfBounds_Err);
    }

    /* Lock MMU for translation */
    ML_$LOCK(MST_LOCK_MMU);

    /* Try to translate virtual to physical address */
    phys_addr = MMU_$VTOP(virt_addr, status);

    if (status[0] == status_$ok) {
        /*
         * Page is already mapped.
         * Wire it if requested (bit 1 set).
         */
        if ((flags & 2) != 0) {
            MMAP_$WIRE(phys_addr);
        }
        ML_$UNLOCK(MST_LOCK_MMU);
        return phys_addr;
    }

    /* Page not mapped - unlock and call MST_$TOUCH to fault it in */
    ML_$UNLOCK(MST_LOCK_MMU);

    /* Call MST_$TOUCH with wire flag based on bit 1 of flags */
    return MST_$TOUCH(virt_addr, status, (flags & 2) != 0 ? 1 : 0);
}
