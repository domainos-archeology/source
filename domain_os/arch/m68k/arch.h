/*
 * arch/m68k/arch.h - M68K Architecture Definitions
 *
 * Main include file for M68K-specific definitions.
 * This aggregates all architecture-specific headers for the M68K platform.
 *
 * When porting to a new architecture, create arch/<arch>/arch.h
 * with equivalent functionality.
 */

#ifndef ARCH_M68K_ARCH_H
#define ARCH_M68K_ARCH_H

/* Interrupt control (DISABLE_INTERRUPTS, ENABLE_INTERRUPTS, etc.) */
#include "arch/m68k/intr.h"

/*
 * M68K memory model:
 *   - Big-endian byte order
 *   - 32-bit pointers
 *   - Natural alignment: 2-byte for 16-bit, 4-byte for 32-bit
 */
#define ARCH_BIG_ENDIAN    1
#define ARCH_PTR_SIZE      4
#define ARCH_ALIGN_16      2
#define ARCH_ALIGN_32      4

#endif /* ARCH_M68K_ARCH_H */
