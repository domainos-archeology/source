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
 * Allocates a physical page and returns its page frame number.
 *
 * Parameters:
 *   ppn_out  - Pointer to receive physical page number
 *   status   - Receives status code
 *
 * Original address: TBD
 */
void WP_$CALLOC(uint32_t *ppn_out, status_$t *status);

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
