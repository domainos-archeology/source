/*
 * WP - Wire Pages Module
 *
 * This module provides memory wiring/pinning operations for Domain/OS.
 * Wired pages are locked in physical memory and won't be paged out.
 */

#ifndef WP_H
#define WP_H

#include "base/base.h"

/*
 * WP_$CALLOC - Wire and allocate memory
 *
 * Allocates and wires memory for a given virtual address.
 *
 * Parameters:
 *   addr_ptr - Pointer to virtual address
 *   status   - Receives status code
 *
 * Returns:
 *   Wired address (to be passed to WP_$UNWIRE when done)
 *
 * Original address: TBD
 */
uint32_t WP_$CALLOC(void *addr_ptr, status_$t *status);

/*
 * WP_$UNWIRE - Unwire previously wired memory
 *
 * Releases a wired memory region obtained from WP_$CALLOC or
 * similar wiring function.
 *
 * Parameters:
 *   wired_addr - Address returned by WP_$CALLOC
 *
 * Original address: TBD
 */
void WP_$UNWIRE(uint32_t wired_addr);

#endif /* WP_H */
