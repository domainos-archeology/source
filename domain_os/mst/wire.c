/*
 * MST_$WIRE - Wire a page into physical memory
 *
 * This function ensures a virtual page is mapped to physical memory
 * and "wires" it so it cannot be paged out. This is used for pages
 * that must remain resident (e.g., I/O buffers, interrupt handlers).
 *
 * If the page is already mapped, it simply wires it. If not mapped,
 * it calls MST_$TOUCH to fault in the page and wire it.
 */

#include "mst.h"

/* ML_$LOCK, ML_$UNLOCK declared in ml/ml.h via mst.h */
extern uint32_t MMU__VTOP(uint32_t virt_addr, status_$t *status);
extern void MMAP__WIRE(uint32_t phys_addr);

/*
 * MST_$WIRE - Wire a virtual page into physical memory
 *
 * @param vpn         Virtual page address
 * @param status_ret  Output: status code
 * @return Physical address of the wired page
 */
uint32_t MST_$WIRE(uint32_t vpn, status_$t *status_ret)
{
    uint32_t phys_addr;
    status_$t status;

    /* Lock MMU for translation */
    ML_$LOCK(MST_LOCK_MMU);

    /* Try to translate virtual to physical address */
    phys_addr = MMU__VTOP(vpn, &status);

    if (status == status_$ok) {
        /*
         * Page is already mapped - wire it and return.
         */
        MMAP__WIRE(phys_addr);
        ML_$UNLOCK(MST_LOCK_MMU);
    } else {
        /*
         * Page not mapped - unlock and call MST_$TOUCH to fault it in.
         * The wire_flag=1 tells TOUCH to wire the page.
         */
        ML_$UNLOCK(MST_LOCK_MMU);
        phys_addr = MST_$TOUCH(vpn, &status, 1);
    }

    *status_ret = status;
    return phys_addr;
}
