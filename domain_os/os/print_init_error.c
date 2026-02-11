/*
 * OS_$PRINT_INIT_ERROR - Print initialization error message
 *
 * Prints an error message during system initialization.
 * Uses ERROR_$PRINT with a null format string.
 *
 * Parameters:
 *   msg - Error message to display
 *
 * Original address: 0x00e6d1cc
 * Size: 24 bytes
 */

#include "os/os_internal.h"
#include "vfmt/vfmt.h"

/* Static null format data at 0xe6d1e4 */
static const uint32_t null_format = 0;

void OS_$PRINT_INIT_ERROR(const char *msg)
{
    ERROR_$PRINT(msg, &null_format, &null_format);
}
