/*
 * PROC1_$FREE_STACK - Free a process stack
 * Original address: 0x00e1511a
 *
 * Returns a stack to the free list if it's in the "large stack" region
 * (above STACK_HIGH_WATER). Small stacks (in the low region) are not
 * actually freed - their memory is lost until system restart.
 *
 * The free list uses the first 4 bytes of the stack as a link pointer.
 *
 * Parameters:
 *   stack - Pointer to top of stack to free
 */

#include "proc1.h"

/* Stack allocation globals */
#if defined(M68K)
    /* Free list of 4KB stacks */
    #define STACK_FREE_LIST     (*(void**)0xe26120)
    /* Current stack allocation high water mark */
    #define STACK_HIGH_WATER    (*(void**)0xe26124)
#else
    extern void *STACK_FREE_LIST;
    extern void *STACK_HIGH_WATER;
#endif

void PROC1_$FREE_STACK(void *stack)
{
    void **link_ptr;

    /*
     * Only free stacks that are above the high water mark
     * (i.e., in the large stack region that grows downward)
     * Small stacks below the high water mark are not freed.
     */
    if ((uint32_t)stack > (uint32_t)STACK_HIGH_WATER) {
        /*
         * Add to free list using first word as link
         * The link is stored at stack - 4 (one word before the stack top)
         */
        link_ptr = (void**)((char*)stack - 4);
        *link_ptr = STACK_FREE_LIST;
        STACK_FREE_LIST = link_ptr;
    }
}
