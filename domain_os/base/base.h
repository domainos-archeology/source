// Base type definitions shared across Domain/OS kernel modules

#ifndef BASE_H
#define BASE_H

// =============================================================================
// Fixed-width integer types (normally from stdint.h)
// m68k: char=8, short=16, int=32, long=32, ptr=32
//
// The kernel is built freestanding (-nostdlib -ffreestanding) so these are
// spelled out for the m68k target.  Host builds (ARCH_HOST, used for the
// unit tests) get them from the host's <stdint.h>/<stddef.h> so they don't
// conflict with the host C library's definitions (e.g. 64-bit uintptr_t).
// =============================================================================
#if defined(ARCH_HOST)
#include <stdint.h>
#include <stddef.h>
#else
typedef signed char int8_t;
typedef short int16_t;
typedef int int32_t;
typedef long long int64_t;

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;

typedef unsigned int uintptr_t;

// =============================================================================
// Types from stddef.h
// =============================================================================
typedef unsigned int size_t;
typedef int ptrdiff_t;
#endif

#ifndef NULL
#define NULL ((void *)0)
#endif

// =============================================================================
// offsetof - needed by the _Static_assert struct-layout checks in the
// subsystem headers.  <stddef.h> is unavailable on the freestanding m68k
// build, so fall back to the compiler builtin.
// =============================================================================
#ifndef offsetof
#define offsetof(type, member) __builtin_offsetof(type, member)
#endif

// =============================================================================
// Basic type aliases
// =============================================================================
typedef unsigned long ulong;
typedef unsigned int uint;
typedef unsigned short ushort;
typedef unsigned char uchar;

typedef void *code_ptr_t;

// =============================================================================
// Status type
// =============================================================================
typedef int32_t status_$t;   /* 32-bit on m68k; keep it 32-bit on 64-bit hosts so embedded layouts hold */

// Common status codes
#define status_$ok 0
#define status_$invalid_line_number 0x000b0007
#define status_$requested_line_or_operation_not_implemented 0x000b000d
#define status_$term_invalid_option 0x000b0004

// TTY status codes (module 0x35) live in tty/tty.h, their owning module.

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
#define BE32_CONST(x)                                                          \
  ((uint32_t)((((x) >> 24) & 0x000000FF) | (((x) >> 8) & 0x0000FF00) |         \
              (((x) << 8) & 0x00FF0000) | (((x) << 24) & 0xFF000000)))
/* Compile-time byte swap for 16-bit constants */
#define BE16_CONST(x)                                                          \
  ((uint16_t)((((x) >> 8) & 0x00FF) | (((x) << 8) & 0xFF00)))
#endif

// =============================================================================
// UID structure - 8 bytes
//
// Memory layout uses high word first (big-endian order).
// UIDs are stored in big-endian format to match on-disk representation.
// =============================================================================
typedef struct uid_t {
  uint32_t high; /* 0x00: High word (timestamp-based) */
  uint32_t low;  /* 0x04: Low word (node ID + counter) */
} uid_t;

/* Layout recovered from the disassembly -- see the field comments above. */
_Static_assert(__builtin_offsetof(uid_t, high) == 0x00, "uid_t.high");
_Static_assert(__builtin_offsetof(uid_t, low) == 0x04, "uid_t.low");

/* UID constant initializer - stores in big-endian format */
#define UID_CONST(high, low) {BE32_CONST(high), BE32_CONST(low)}

/*
 * UID_$NIL and the other well-known object UIDs are declared in uid/uid.h,
 * which includes this header for uid_t (bead source-3uo).  Declaring UID_$NIL
 * here as well duplicated it.
 */

// =============================================================================
// Clock type
// =============================================================================
// 48-bit clock value used by Domain/OS calendar functions
// Represents time in 4-microsecond ticks since epoch (250,000 ticks/sec)
// Constant 0x3D090 = 250,000 (ticks per second)
// Constant 0xD090 = 0x3D090 & 0xFFFF (low word for multiplication)
//
// The record is SIX bytes wide in the image (TIME_$CLOCK writes it as
// "move.l Dn,(An)+" / "move.w Dn,(An)", and every embedded copy - e.g.
// tpad_$globals_t+0x04, audit_data_t+0x3A, ring_$unit_t+0x568 - sits on a
// six-byte stride).  m68k-elf-gcc already gives it size 6 / alignment 2, but
// a 64-bit host would pad it to 8 with alignment 4 and every record that
// embeds it would then have a host layout different from the image, so the
// packed spelling is stated explicitly.  (source-no75)
typedef struct __attribute__((packed, aligned(2))) {
  uint high;  // upper 32 bits
  ushort low; // lower 16 bits
} clock_t;

_Static_assert(sizeof(clock_t) == 6, "clock_t is six bytes in the image");
_Static_assert(_Alignof(clock_t) == 2, "clock_t is word-aligned");

// =============================================================================
// Boolean type (Domain/OS style)
// =============================================================================
/* Domain Pascal booleans are a signed byte: 0xFF is true and the code tests
 * them with tst.b / bmi, i.e. "< 0".  int8_t (not char) keeps that test
 * byte-order and host-signedness independent (char is unsigned on some
 * hosts, e.g. Linux aarch64). */
typedef int8_t boolean;
#define true ((boolean) - 1) // 0xFF in Domain/OS convention
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
#include "arch/arch.h"

#endif /* BASE_H */
