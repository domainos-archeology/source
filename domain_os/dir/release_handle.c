/*
 * dir_$release_handle - Release directory handle completely
 *
 * Full cleanup of a directory handle. Calls the three-step
 * release sequence: unlock, unmap, free. Then clears the
 * handle pointer to NULL. No-op if handle is already NULL.
 *
 * Takes a pointer to the handle variable so it can clear it
 * after release, preventing double-free.
 *
 * Original address: 0x00E4B9D6
 * Original size: 44 bytes
 */

#include "dir/dir_internal.h"

void dir_$release_handle(void *handle_ptr)
{
    int32_t *hp = (int32_t *)handle_ptr;

    if (*hp != 0) {
        /* Step 1: Unlock - release read/write lock */
        FUN_00e4b234((void *)(uintptr_t)*hp);

        /* Step 2: Unmap - release mapped pages */
        FUN_00e4b6ba((void *)(uintptr_t)*hp);

        /* Step 3: Free - return handle slot to free list */
        FUN_00e4b980((void *)(uintptr_t)*hp);

        /* Clear the handle pointer */
        *hp = 0;
    }
}
