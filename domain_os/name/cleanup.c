/*
 * NAME_$CLEANUP - Clean up naming resources
 *
 * Simple wrapper that delegates to DIR_$CLEANUP.
 *
 * Original address: 0x00e5860e
 * Original size: 14 bytes
 */

#include "name/name_internal.h"

/* Forward declaration for DIR subsystem */
extern void DIR_$CLEANUP(void);

/*
 * NAME_$CLEANUP - Clean up naming resources
 *
 * Called during system shutdown or process cleanup to release
 * naming-related resources.
 */
void NAME_$CLEANUP(void)
{
    DIR_$CLEANUP();
}
