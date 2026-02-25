/*
 * arch/arch.h - Architecture Selector
 *
 * Includes the correct architecture-specific header based on the
 * build-time ARCH_* define. Each architecture header must provide:
 *
 *   - Interrupt control macros:
 *       DISABLE_INTERRUPTS(sr)  - Save SR and disable all interrupts
 *       ENABLE_INTERRUPTS(sr)   - Restore saved SR
 *       GET_SR(sr_var)          - Read current status register
 *       SET_SR(sr_val)          - Write status register
 *
 *   - SR constants:
 *       SR_IPL_MASK, SR_IPL_DISABLE_ALL, SR_SUPERVISOR, SR_TRACE
 *
 *   - Memory model constants:
 *       ARCH_BIG_ENDIAN (if applicable), ARCH_PTR_SIZE,
 *       ARCH_ALIGN_16, ARCH_ALIGN_32
 *
 *   - Global data pointer:
 *       __A5_BASE()  (or a stub for non-M68K)
 *
 * To add a new architecture, create arch/<arch>/arch.h and add
 * a new #elif block here.
 */

#ifndef ARCH_H
#define ARCH_H

#if defined(ARCH_M68K)
#include "arch/m68k/arch.h"
#elif defined(ARCH_HOST)
#include "arch/host/arch.h"
#else
#error "undefined architecture: define ARCH_M68K or ARCH_HOST"
#endif

#endif /* ARCH_H */