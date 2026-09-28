/*
 * PMAP_$INIT_WS_SCAN - Give a process its working-set index and scan timer
 *
 * 0x00E145F0 - 0x00E146B2 (196 bytes, A5 = 0xE24D44).  Re-emitted from the
 * disassembly 2026-09-27.  Wrong before: the timer element was taken from
 * a 0x1A-stride array (the image steps 0x1C: `idx * 0x1C` from A5 + 0x24,
 * 0x00E14626 - 0x00E14632), the queue was a PMAP-private array (the image
 * uses 0xE2A4A0 - 0xC + idx * 0xC, i.e. TIME_$VTQ[idx - 1]), the expiry
 * was 0 (the image stores {3, 0xD090} = 250000, 0x00E1465E - 0x00E1466A)
 * and the interval was a copy of that same 48-bit value, not 250000 in
 * the high longword.
 *
 * Arguments: (0x8,A6) index word (the pid, D2), (0xa,A6) param word (the
 * WSL index, D3).  MMAP_$SET_WS_INDEX(index, &param) first; a param of 5
 * (the wired pool) gets no timer.
 */

#include "pmap/pmap_internal.h"
#include "mmap/mmap.h"

/* 0x00E14658: element flags */
#define PMAP_WS_TIMER_FLAGS     0x1A
/* 0x00E14662: `move.l #0x3d090,(0x32,A2)` after `clr.w (0x30,A2)` -
 * the 48-bit clock {3, 0xD090} = 250000 */
#define PMAP_WS_TIMER_HIGH      0x00000003u
#define PMAP_WS_TIMER_LOW       0xD090u
/* 0x00E1461A: the wired pool never gets a scan timer */
#define PMAP_WS_INDEX_WIRED     5

void PMAP_$INIT_WS_SCAN(uint16_t index, int16_t param)
{
    uint16_t local_param;       /* (-0xE,A6) */
    status_$t status;           /* (-0xC,A6) */
    clock_t when;               /* (-0x8,A6) */
    time_queue_t *queue;        /* 0xE2A4A0 - 0xC + index * 0xC */
    time_queue_elem_t *e;       /* A2 + 0x24 */

    /* 0x00E14606 - 0x00E14618: result slot discarded */
    local_param = (uint16_t)param;
    MMAP_$SET_WS_INDEX(index, &local_param);

    /* 0x00E1461A */
    if (param == PMAP_WS_INDEX_WIRED) {
        return;
    }

    /* 0x00E14622 - 0x00E14650 */
    queue = &TIME_$VTQ[index - 1];
    e = PMAP_WS_TIMER_ELEM(index);
    TIME_$Q_REMOVE_ELEM(queue, e, &status);

    /* 0x00E14654 - 0x00E14682 */
    e->flags = PMAP_WS_TIMER_FLAGS;
    e->expire_high = PMAP_WS_TIMER_HIGH;
    e->expire_low = PMAP_WS_TIMER_LOW;
    e->interval_high = e->expire_high;                  /* move.l (0x30,A2),(0x38,A2) */
    e->interval_low = e->expire_low;                    /* move.w (0x34,A2),(0x3c,A2) */
    e->callback = ARCH_PTR_TO_VA(PMAP_$WS_SCAN_CALLBACK);
    e->callback_arg = (uint32_t)index;

    /* 0x00E14686 - 0x00E146A4 */
    when.high = 0;
    when.low = 0;
    TIME_$Q_ENTER_ELEM(queue, &when, e, &status);
}
