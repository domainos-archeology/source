/*
 * arch/host/intr.h - Host/Testing Interrupt Control Stubs
 *
 * No-op implementations of the portable interrupt control macros
 * for building and testing on the host (development) machine.
 *
 * These macros have no effect since the host environment does not
 * have hardware interrupt priority levels. They exist so that kernel
 * code compiles and runs in unit tests without modification.
 *
 * For multi-threaded host tests, a future version could use
 * pthread mutexes here. For now, single-threaded tests are assumed.
 *
 * NOTE: This header is included via base/base.h -> arch/arch.h ->
 * arch/host/arch.h, so base integer types (uint16_t, etc.) are
 * already defined by the time this file is processed.
 */

#ifndef ARCH_HOST_INTR_H
#define ARCH_HOST_INTR_H

/*
 * Status Register (SR) bit definitions
 *
 * These constants are provided for source compatibility with code
 * that references them directly. Their numeric values match the
 * M68K definitions so that constant expressions evaluate identically.
 */
#define SR_IPL_MASK             0x0700  /* Interrupt priority level bits */
#define SR_IPL_DISABLE_ALL      0x0700  /* Disable all interrupts (IPL=7) */
#define SR_SUPERVISOR           0x2000  /* Supervisor mode bit */
#define SR_TRACE                0x8000  /* Trace mode bit */

/*
 * Host interrupt nesting counter.
 *
 * Tracks logical interrupt disable depth so that tests can verify
 * proper nesting of DISABLE_INTERRUPTS / ENABLE_INTERRUPTS pairs.
 * Declared as a global; the test or host runtime must provide the
 * definition (e.g., `int __host_intr_disable_count = 0;`).
 */
extern int __host_intr_disable_count;

/*
 * DISABLE_INTERRUPTS - Save state and logically disable interrupts
 *
 * On host: saves current nesting depth into sr, increments counter.
 * The sr variable is typically declared as uint16_t by caller code;
 * the cast ensures no truncation warnings.
 */
#define DISABLE_INTERRUPTS(sr) \
    do { \
        (sr) = (uint16_t)__host_intr_disable_count; \
        __host_intr_disable_count++; \
    } while (0)

/*
 * ENABLE_INTERRUPTS - Restore previously saved interrupt state
 *
 * On host: restores nesting counter to the saved value.
 */
#define ENABLE_INTERRUPTS(sr) \
    do { \
        __host_intr_disable_count = (int)(uint16_t)(sr); \
    } while (0)

/*
 * GET_SR - Read the current (simulated) status register
 *
 * On host: returns SR_IPL_DISABLE_ALL if interrupts are logically
 * disabled, 0 otherwise. This allows code that tests SR bits to
 * function correctly.
 */
#define GET_SR(sr_var) \
    do { \
        (sr_var) = (__host_intr_disable_count > 0) \
                       ? (uint16_t)SR_IPL_DISABLE_ALL \
                       : (uint16_t)0; \
    } while (0)

/*
 * SET_SR - Write to the (simulated) status register
 *
 * On host: if the value has IPL bits set, logically disable;
 * otherwise logically enable (reset counter to 0).
 */
#define SET_SR(sr_val) \
    do { \
        if ((uint16_t)(sr_val) & SR_IPL_DISABLE_ALL) { \
            __host_intr_disable_count++; \
        } else { \
            __host_intr_disable_count = 0; \
        } \
    } while (0)

#endif /* ARCH_HOST_INTR_H */
