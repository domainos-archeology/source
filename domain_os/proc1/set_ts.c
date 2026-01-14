/*
 * PROC1_$SET_TS - Set timeslice for a process
 * Original address: 0x00e14a08
 *
 * Schedules a timeslice timer callback for a process. Uses the kernel's
 * timer queue mechanism to trigger PROC1_$TS_END_CALLBACK when the
 * timeslice expires.
 *
 * Parameters:
 *   pcb - Process control block pointer
 *   timeslice - Timeslice value (in timer ticks)
 */

#include "proc1.h"

/*
 * Timer callback structure for timeslice management
 * Base at 0xe254e8, 0x1c (28) bytes per process
 */
typedef struct ts_timer_entry_t {
    uint32_t field_00;          /* 0x00: Unknown */
    uint32_t field_04;          /* 0x04: Unknown */
    uint32_t field_08;          /* 0x08: Unknown */
    uint32_t field_0c;          /* 0x0C: Unknown */
    uint32_t field_10;          /* 0x10: Unknown */
    void     *callback_info;    /* 0x14: Pointer to callback info block */
    void     (*callback)(void*);/* 0x18: Callback function pointer */
    uint32_t callback_param;    /* 0x1C: Callback parameter (PID) */
    uint32_t cpu_time_high;     /* 0x20: CPU time high word */
    uint16_t cpu_time_low;      /* 0x24: CPU time low word */
    uint16_t field_26;          /* 0x26: Unknown */
} ts_timer_entry_t;

/* External timer queue re-enter function */
extern void TIME_$Q_REENTER_ELEM(void *queue_elem, void *time, uint16_t flags,
                                  void *dest_time, void *callback_info,
                                  status_$t *status);

/* Timer callback table base - 0xe254e8 */
#if defined(M68K)
    #define TS_TIMER_TABLE_BASE     ((ts_timer_entry_t*)0xe254e8)
    #define TS_QUEUE_TABLE_BASE     ((void*)0xe2a494)
#else
    extern ts_timer_entry_t TS_TIMER_TABLE_BASE[];
    extern void *TS_QUEUE_TABLE_BASE;
#endif

/* Size of timer queue element (12 bytes) */
#define TS_QUEUE_ELEM_SIZE  12

void PROC1_$SET_TS(proc1_t *pcb, int16_t timeslice)
{
    status_$t status;
    uint32_t time_high;
    uint16_t time_low;
    uint16_t pid;
    char *queue_elem;
    char *callback_info;

    pid = pcb->mypid;

    /* Build time value (6-byte: 4 high + 2 low) */
    time_high = 0;
    time_low = timeslice;

    /*
     * Calculate timer table entry address:
     * entry = base + pid * 0x1c
     * The assembly does: D2 = pid << 2, D3 = D2, D2 = -D2, D3 <<= 3, D2 += D3
     * Which is: D2 = pid * (-4 + 8) = pid * 4, then adds to get pid * 0x1c
     * Actually: D2 = -(pid*4) + (pid*4*2) = pid*4 = NOT the offset
     * Let me re-read: lsl.w #2 => *4, then neg => -4x, then lsl.w #3 => *8
     * So: -4x + 8x = 4x... that's wrong for 0x1c
     *
     * Wait, looking more carefully:
     * D0w = pid * 4, D1w = D0w = pid*4, D0w = -D0w = -pid*4
     * D1w <<= 3 => D1w = pid*32, D0w += D1w => -4x + 32x = 28x = 0x1c * pid
     */
    callback_info = (char*)TS_TIMER_TABLE_BASE + (pid * 0x1c) + 0x14;

    /* Calculate queue element address: base + pid * 12 */
    queue_elem = (char*)TS_QUEUE_TABLE_BASE + (pid * TS_QUEUE_ELEM_SIZE) - 0xC;

    /* Schedule the timer callback */
    TIME_$Q_REENTER_ELEM(queue_elem, &time_high, 0, &pcb->cpu_total,
                         callback_info, &status);
}
