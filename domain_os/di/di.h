/*
 * DI - Deferred Interrupt Module
 *
 * Provides deferred interrupt handling for Domain/OS.
 * Deferred interrupts are scheduled to run at a later time,
 * typically after the current interrupt handler completes.
 *
 * The queue uses a singly-linked list with elements containing
 * callback arguments. When an interrupt cannot be processed
 * immediately, it is enqueued for later processing.
 */

#ifndef DI_H
#define DI_H

#include "base/base.h"

/*
 * DI queue element structure - 16 bytes
 *
 * Used for deferred interrupt handling. Elements are linked
 * into a queue and processed when conditions allow.
 *
 * Layout from assembly analysis:
 *   Offset 0x00: next pointer (to next element in queue)
 *   Offset 0x04: arg1 (first callback argument)
 *   Offset 0x08: arg2 (second callback argument)
 *   Offset 0x0C: enqueued flag (0xFF = enqueued, 0 = not enqueued)
 *   Offset 0x0D-0x0F: padding/reserved
 */
typedef struct di_queue_elem_t {
    struct di_queue_elem_t *next;  /* 0x00: Next element in queue */
    uint32_t arg1;                 /* 0x04: First callback argument */
    uint32_t arg2;                 /* 0x08: Second callback argument */
    uint8_t enqueued;              /* 0x0C: Non-zero (0xFF) if enqueued */
    uint8_t reserved[3];           /* 0x0D-0x0F: Padding */
} di_queue_elem_t;

/* Layout recovered from the disassembly -- see the field comments above. */
#if defined(ARCH_M68K)
_Static_assert(__builtin_offsetof(di_queue_elem_t, next) == 0x00, "di_queue_elem_t.next");
_Static_assert(__builtin_offsetof(di_queue_elem_t, arg1) == 0x04, "di_queue_elem_t.arg1");
_Static_assert(__builtin_offsetof(di_queue_elem_t, arg2) == 0x08, "di_queue_elem_t.arg2");
_Static_assert(__builtin_offsetof(di_queue_elem_t, enqueued) == 0x0C, "di_queue_elem_t.enqueued");
_Static_assert(__builtin_offsetof(di_queue_elem_t, reserved) == 0x0D, "di_queue_elem_t.reserved");
#endif

/*
 * Global queue head pointer
 * Points to the first element in the deferred interrupt queue.
 * Located at address 0x00e20602 in the original binary.
 */
extern di_queue_elem_t *DI_$Q_HEAD;

/*
 * DI_$INIT_Q_ELEM - Initialize a DI queue element
 *
 * Clears all 16 bytes of the queue element structure, setting
 * next pointer, arguments, and enqueued flag to zero.
 *
 * Parameters:
 *   elem - Pointer to the DI queue element to initialize
 *
 * From: 0x00e209d6
 */
void DI_$INIT_Q_ELEM(di_queue_elem_t *elem);

/*
 * DI_$ENQ - Enqueue a deferred interrupt
 *
 * Adds a deferred interrupt element to the head of the queue.
 * The element must not already be enqueued (will crash if so).
 * After enqueuing, the enqueued flag is set to 0xFF.
 *
 * Parameters:
 *   arg1 - First callback argument to store in element
 *   arg2 - Second callback argument to store in element
 *   elem - Pointer to the DI queue element to enqueue
 *
 * From: 0x00e209a8
 */
void DI_$ENQ(uint32_t arg1, uint32_t arg2, di_queue_elem_t *elem);

/*
 * Status DI_$ENQ crashes with when an element is enqueued twice: the
 * longword cell at 0x00E20DF4 (`pea (0x440,PC)` at 0x00E209B2), bytes
 * 00 0a 00 0c, "bad deferred interrupt queue" (module 0x0A = PROC1).
 */
#define status_$proc1_bad_deferred_interrupt_queue 0x000A000C

#endif /* DI_H */
