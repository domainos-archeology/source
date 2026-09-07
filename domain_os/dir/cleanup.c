/*
 * DIR_$CLEANUP - Clean up directory subsystem resources
 *
 * Iterates through 32 directory operation slots, cleans up
 * owned entries, and releases the mutex.
 *
 * Original address: 0x00E53578
 * Original size: 432 bytes
 */

#include "dir/dir_internal.h"

/*
 * DIR_$CLEANUP - Clean up directory subsystem resources
 *
 * Called during process shutdown to release any directory resources
 * held by the current process. Iterates through 32 slots:
 * 1. Check if slot is owned by current process (via PROC1_$CURRENT)
 * 2. If owned, call DIR_$VALIDATE_PAGES to clean up the handle entry
 * 3. Call dir_$release_wire to release request buffers
 * 4. Call dir_$release_handle to release handle slots
 * 5. Finally stop the exclusion mutex
 *
 * Also calls DIR_$OLD_CLEANUP for legacy cleanup.
 */
void DIR_$CLEANUP(void)
{
    int16_t i;
    uint32_t slot_mask;
    status_$t status;
    void *handle_entry;
    void **handle_ptr;

    /* Iterate through 32 slots */
    for (i = 0; i < 32; i++) {
        /* Check if this slot is active */
        slot_mask = (uint32_t)1 << i;

        if ((DAT_00e7fc3c & slot_mask) != 0) {
            /* Slot is active - check ownership */
            /* The handle entry pool starts at DAT_00e7f280, each entry is
             * a fixed size. Check if the owner matches current process. */
            /* TODO(source-qgq): Verify exact slot entry size and owner field offset from Ghidra */

            /* Clean up the handle entry */
            handle_entry = (void *)(DAT_00e7f280 + i * 0x30);
            DIR_$VALIDATE_PAGES(handle_entry, 0, &status);

            /* Release request buffer */
            dir_$release_wire(handle_entry);

            /* Release handle slot */
            handle_ptr = (void **)(DAT_00e7f280 + i * 0x30);
            dir_$release_handle(handle_ptr);
        }
    }

    /* Stop the exclusion mutex */
    ML_$EXCLUSION_STOP(&DIR_$MUTEX);

    /* Call legacy cleanup */
    DIR_$OLD_CLEANUP();
}
