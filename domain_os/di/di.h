/*
 * DI - Deferred Interrupt Module
 *
 * Provides deferred interrupt handling for Domain/OS.
 * Deferred interrupts are scheduled to run at a later time,
 * typically after the current interrupt handler completes.
 */

#ifndef DI_H
#define DI_H

#include "base/base.h"

/*
 * DI queue element structure - 16 bytes
 *
 * Used for deferred interrupt handling. Elements are linked
 * into a queue and processed when conditions allow.
 */
typedef struct di_queue_elem_t {
    uint32_t next;          /* 0x00: Next element pointer */
    uint32_t prev;          /* 0x04: Previous element pointer */
    uint32_t handler;       /* 0x08: Handler function pointer */
    uint32_t arg;           /* 0x0C: Handler argument */
} di_queue_elem_t;

/*
 * DI_$INIT_Q_ELEM - Initialize a DI queue element
 *
 * Initializes a deferred interrupt queue element for use.
 * Sets up the list pointers and clears handler fields.
 *
 * Parameters:
 *   elem - Pointer to the DI queue element to initialize
 */
void DI_$INIT_Q_ELEM(di_queue_elem_t *elem);

/*
 * DI_$ENQUEUE - Add element to deferred interrupt queue
 *
 * Adds a deferred interrupt element to the queue for later processing.
 *
 * Parameters:
 *   elem - Pointer to the DI queue element
 */
void DI_$ENQUEUE(di_queue_elem_t *elem);

/*
 * DI_$DEQUEUE - Remove element from deferred interrupt queue
 *
 * Removes a deferred interrupt element from the queue.
 *
 * Parameters:
 *   elem - Pointer to the DI queue element
 */
void DI_$DEQUEUE(di_queue_elem_t *elem);

/*
 * DI_$PROCESS - Process pending deferred interrupts
 *
 * Processes all pending deferred interrupt elements in the queue.
 */
void DI_$PROCESS(void);

#endif /* DI_H */
