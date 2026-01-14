// Base type definitions shared across Domain/OS kernel modules

#ifndef BASE_H
#define BASE_H

// =============================================================================
// Fixed-width integer types (normally from stdint.h)
// m68k: char=8, short=16, int=32, long=32, ptr=32
// =============================================================================
typedef signed char         int8_t;
typedef short               int16_t;
typedef int                 int32_t;
typedef long long           int64_t;

typedef unsigned char       uint8_t;
typedef unsigned short      uint16_t;
typedef unsigned int        uint32_t;
typedef unsigned long long  uint64_t;

typedef unsigned int        uintptr_t;

// =============================================================================
// Types from stddef.h
// =============================================================================
typedef unsigned int        size_t;
typedef int                 ptrdiff_t;

#ifndef NULL
#define NULL ((void *)0)
#endif

// =============================================================================
// Basic type aliases
// =============================================================================
typedef unsigned long ulong;
typedef unsigned int uint;
typedef unsigned short ushort;
typedef unsigned char uchar;

// =============================================================================
// Status type
// =============================================================================
typedef long status_$t;

// Common status codes
#define status_$ok                                          0
#define status_$invalid_line_number                         0x000b0007
#define status_$requested_line_or_operation_not_implemented 0x000b000d
#define status_$term_invalid_option                         0x000b0004

// TTY status codes (module 0x35)
#define status_$tty_access_denied                           0x00350001
#define status_$tty_invalid_function                        0x00350002
#define status_$tty_buffer_full                             0x00350004
#define status_$tty_eof                                     0x00350005
#define status_$tty_invalid_count                           0x00350006
#define status_$tty_quit_signalled                          0x00350007
#define status_$tty_hangup                                  0x00350009
#define status_$tty_would_block                             0x0035000a

// =============================================================================
// m68k pointer type
// 32-bit on original hardware. Use uint32_t for structure layout,
// cast to actual pointer when dereferencing on host.
// =============================================================================
typedef uint32_t m68k_ptr_t;

// =============================================================================
// UID structure (8 bytes on m68k)
// =============================================================================
typedef struct {
    uint32_t high;
    uint32_t low;
} uid_$t;

// Alternate name for compatibility
typedef uid_$t uid_t;

// UID_$NIL - nil UID value (extern, defined in kernel)
extern uid_$t UID_$NIL;

// =============================================================================
// Clock type
// =============================================================================
typedef long clock_t;

// =============================================================================
// Boolean type (Domain/OS style)
// =============================================================================
typedef char boolean;
#define true  ((boolean)-1)  // 0xFF in Domain/OS convention
#define false ((boolean)0)

// =============================================================================
// Architecture-specific includes
// =============================================================================
// Include architecture-specific definitions. When porting to a new
// architecture, create arch/<arch>/arch.h with equivalent functionality.
#if defined(__m68k__) || defined(M68K) || !defined(__x86_64__)
// Default to m68k for now (Domain/OS target)
#include "arch/m68k/arch.h"
#endif

#endif /* BASE_H */
