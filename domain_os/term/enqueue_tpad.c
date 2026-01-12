#include "term.h"

// External functions
extern void TPAD_$DATA(void);
extern short M_$OIS_WLW(long value, short modulus);

// Structure for TPAD queue management
// The queue appears to be a circular buffer with head and tail indices
typedef struct {
    short head;      // offset 0: current head position
    short tail;      // offset 2: current tail position
    // data entries follow at offset 4, each 16 bytes (0x10)
} tpad_queue_t;

// Enqueues pending TPAD (Touch Pad?) data entries.
//
// Processes entries from the queue's tail up to the head position.
// For each entry, calls TPAD_$DATA, then advances the tail using
// modular arithmetic (wrapping at 6 entries).
//
// The parameter is a pointer to a pointer to the queue structure.
void TERM_$ENQUEUE_TPAD(void **param1) {
    tpad_queue_t *queue;
    short head, tail;

    queue = *(tpad_queue_t **)*param1;
    head = queue->head;
    tail = queue->tail;

    while (head != tail) {
        // Call TPAD_$DATA with pointer to entry at (base + 4 + tail * 16)
        TPAD_$DATA();

        // Advance tail with wrap-around at 6 entries
        tail = M_$OIS_WLW((long)(tail + 1), 6);
        queue->tail = tail;
    }
}
