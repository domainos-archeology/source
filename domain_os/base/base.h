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

typedef void* code_ptr_t;

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
// Endian support for portable code
//
// UIDs and other on-disk structures are stored in big-endian format.
// These macros ensure correct byte order on both big and little endian hosts.
// =============================================================================

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define BE32_CONST(x) (x)
#define BE16_CONST(x) (x)
#else
/* Compile-time byte swap for 32-bit constants */
#define BE32_CONST(x) \
    ((uint32_t)((((x) >> 24) & 0x000000FF) | \
                (((x) >>  8) & 0x0000FF00) | \
                (((x) <<  8) & 0x00FF0000) | \
                (((x) << 24) & 0xFF000000)))
/* Compile-time byte swap for 16-bit constants */
#define BE16_CONST(x) \
    ((uint16_t)((((x) >> 8) & 0x00FF) | \
                (((x) << 8) & 0xFF00)))
#endif

// =============================================================================
// UID structure - 8 bytes
//
// Memory layout uses high word first (big-endian order).
// UIDs are stored in big-endian format to match on-disk representation.
// =============================================================================
typedef struct uid_t {
    uint32_t high;      /* 0x00: High word (timestamp-based) */
    uint32_t low;       /* 0x04: Low word (node ID + counter) */
} uid_t;

/* UID constant initializer - stores in big-endian format */
#define UID_CONST(high, low) { BE32_CONST(high), BE32_CONST(low) }

/*
 * UID_$NIL - The nil/empty UID (all zeros)
 *
 * Used to represent "no UID" or an uninitialized UID.
 * Original address: (data constant)
 */
extern uid_t UID_$NIL;


// =============================================================================
// Clock type
// =============================================================================
// 48-bit clock value used by Domain/OS calendar functions
// Represents time in 4-microsecond ticks since epoch (250,000 ticks/sec)
// Constant 0x3D090 = 250,000 (ticks per second)
// Constant 0xD090 = 0x3D090 & 0xFFFF (low word for multiplication)
typedef struct {
  uint high;  // upper 32 bits
  ushort low; // lower 16 bits
} clock_t;

// =============================================================================
// Boolean type (Domain/OS style)
// =============================================================================
typedef char boolean;
#define true  ((boolean)-1)  // 0xFF in Domain/OS convention
#define false ((boolean)0)

// =============================================================================
// Compiler attributes
// =============================================================================
#if defined(__GNUC__) || defined(__clang__)
#define NORETURN __attribute__((noreturn))
#elif defined(_MSC_VER)
#define NORETURN __declspec(noreturn)
#else
#define NORETURN
#endif

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
