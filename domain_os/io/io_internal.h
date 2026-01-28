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
 * IO_$INT_STACK_BASE - M68K interrupt stack top address
 *
 * On the original M68K hardware, the interrupt stack occupies a fixed
 * region with its top (highest address, since the stack grows downward)
 * at 0x00EB2BE8.
 */
#if defined(ARCH_M68K)
#define IO_$INT_STACK_BASE 0x00EB2BE8
#endif

/*
 * IO_$INT_STACK_SIZE - Size of interrupt stack buffer (non-M68K builds)
 *
 * 1024 bytes should be sufficient for interrupt handlers, which are
 * expected to be brief and delegate to deferred processing.
 */
#if !defined(ARCH_M68K)
#define IO_$INT_STACK_SIZE 1024
#endif

#endif /* IO_INTERNAL_H */
