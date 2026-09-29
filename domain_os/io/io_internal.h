/*
 * IO Internal - I/O Subsystem Internal Definitions
 *
 * This header contains internal definitions used within the IO subsystem.
 * External code should use io/io.h instead.
 */

#ifndef IO_INTERNAL_H
#define IO_INTERNAL_H

#include "io/io.h"

/*
 * ============================================================================
 * Architecture-Specific Constants
 * ============================================================================
 */

/*
 * The interrupt stack (top 0x00EB2BE8, in the SAU2 map's STACK segment) is
 * the block OS_$STACK (os/os.h; source-4k71).  It is reached only by the
 * hand-written io/sau2/use_int_stack.s and proc1/sau2/int_handler.s, as
 * `OS_$STACK + 0x2BE8`.  source-702z removed an unused C spelling of it and
 * a host-only stand-in buffer.
 */

#endif /* IO_INTERNAL_H */
