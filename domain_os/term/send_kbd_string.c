#include "term/term_internal.h"

// Sends a keyboard string to the terminal subsystem.
//
// This function wraps KBD_$PUT and converts the resulting status code.
// The first two parameters to KBD_$PUT are both set to an internal constant
// (appears to be a static data address used for this purpose).
void TERM_$SEND_KBD_STRING(void *str, void *length) {
    static uint16_t internal_buffer;  // at 0xe1ab26 in original
    status_$t status;

    KBD_$PUT(&internal_buffer, &internal_buffer, str, length, &status);
    TERM_$STATUS_CONVERT(&status);
}
