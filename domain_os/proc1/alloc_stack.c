/*
 * PROC1_$ALLOC_STACK - Allocate a process stack
 * Original address: 0x00e1501a
 *
 * Allocates memory for a process stack. Uses a two-tier allocation:
 * - Small stacks (< 4KB) grow upward from a low base
 * - Large stacks (>= 4KB) may come from a free list or grow downward
 *
 * Parameters:
 *   size - Requested stack size (will be rounded up to 1KB boundary)
 *   status_ret - Status return pointer
 *
 * Returns:
 *   Pointer to top of allocated stack, or NULL on failure
 */

#include "proc1.h"

/* Stack allocation globals (offsets from 0xe254e8 base) */
#if defined(M68K)
    /* Free list of 4KB stacks */
    #define STACK_FREE_LIST     (*(void**)0xe26120)  /* base + 0xc38 */
    /* Current stack allocation high water mark (grows down) */
    #define STACK_HIGH_WATER    (*(void**)0xe26124)  /* base + 0xc3c */
    /* Current stack allocation low water mark (grows up) */
    #define STACK_LOW_WATER     (*(void**)0xe26128)  /* base + 0xc40 */
#else
    extern void *STACK_FREE_LIST;
    extern void *STACK_HIGH_WATER;
    extern void *STACK_LOW_WATER;
#endif

/* External functions */
extern void ML_$LOCK(uint16_t lock_id);
extern void ML_$UNLOCK(uint16_t lock_id);
extern void WP_$CALLOC(uint32_t *page_out, status_$t *status);
extern void MMU_$INSTALL(uint32_t page, uint32_t vaddr, uint16_t flags);

/* Stack page size constants */
#define STACK_PAGE_SIZE     0x400   /* 1KB pages */
#define STACK_MIN_LARGE     0x1000  /* 4KB threshold for "large" stacks */

/* Status code for no stack space */
#define status_$no_stack_space_is_available 0x000A0009

void *PROC1_$ALLOC_STACK(int16_t size, status_$t *status_ret)
{
    uint16_t rounded_size;
    void *stack_top;
    void *alloc_ptr;
    uint32_t page;
    int is_small_stack;

    *status_ret = status_$ok;

    /* Acquire creation lock */
    ML_$LOCK(PROC1_CREATE_LOCK_ID);

    /* Round size up to 1KB boundary */
    rounded_size = (size + 0x3FF) & ~0x3FF;

    /* Determine if this is a small stack (< 4KB) */
    is_small_stack = (rounded_size < STACK_MIN_LARGE);

    if (is_small_stack) {
        /*
         * Small stack: grows upward from STACK_LOW_WATER
         * Calculate new top: current_low + rounded_size + one page
         */
        stack_top = (char*)STACK_LOW_WATER + rounded_size + STACK_PAGE_SIZE;

        /* Check if we have room (can't cross STACK_HIGH_WATER) */
        if ((uint32_t)stack_top > (uint32_t)STACK_HIGH_WATER) {
            goto no_space;
        }

        alloc_ptr = stack_top;
    } else {
        /*
         * Large stack: check free list first for exactly 4KB stacks
         */
        if (STACK_FREE_LIST != NULL && rounded_size == STACK_MIN_LARGE) {
            /* Pop from free list */
            stack_top = (char*)STACK_FREE_LIST + 4;  /* Return pointer past link */
            STACK_FREE_LIST = *(void**)STACK_FREE_LIST;
            goto done;
        }

        /*
         * Large stack: grows downward from STACK_HIGH_WATER
         * Calculate new position
         */
        alloc_ptr = (char*)STACK_HIGH_WATER - rounded_size - STACK_PAGE_SIZE;

        /* Check if we have room (can't cross STACK_LOW_WATER) */
        if ((uint32_t)alloc_ptr < (uint32_t)STACK_LOW_WATER) {
            goto no_space;
        }

        stack_top = STACK_HIGH_WATER;
    }

    /* Allocate and map pages */
    while (rounded_size != 0) {
        WP_$CALLOC(&page, status_ret);
        if (*status_ret != status_$ok) {
            goto no_space;
        }

        /* Map page at (stack_top - remaining_size) with flags 0x16 */
        MMU_$INSTALL(page, (uint32_t)stack_top - rounded_size, 0x16);

        rounded_size -= STACK_PAGE_SIZE;
    }

    /* Update water marks */
    if (is_small_stack) {
        STACK_LOW_WATER = stack_top;
    } else {
        STACK_HIGH_WATER = alloc_ptr;
    }

    goto done;

no_space:
    *status_ret = status_$no_stack_space_is_available;

done:
    ML_$UNLOCK(PROC1_CREATE_LOCK_ID);
    return stack_top;
}
