#include "term/term_internal.h"

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
        // 0xe7248c: move.w (A2),D0 ; lsl.w #4,D0 ; pea (0x4,A3,D0.w) ; jsr TPAD_$DATA
        TPAD_$DATA((uint32_t *)((char *)queue + 4 + (tail << 4)));

        // Advance tail with wrap-around at 6 entries
        // 0xe7249c: ext.l D0 ; addq.l #1,D0 ; jsr M$OIS$WLW(D0, 6)
        tail = M$OIS$WLW((long)tail + 1, 6);
        queue->tail = tail;
    }
}
