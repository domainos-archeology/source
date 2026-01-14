/*
 * TIME_$SET_CPU_LIMIT - Set CPU time limit
 *
 * Sets a CPU time limit for the current process.
 * When the limit is exceeded, SIGXCPU is sent.
 *
 * Parameters:
 *   limit - Pointer to limit value (clock_t)
 *   relative - Pointer to flag: 0 = absolute, non-zero = relative to current CPU time
 *   status - Status return
 *
 * Original address: 0x00e58f64
 */

#include "time.h"
#include "proc1/proc1.h"
#include "proc2/proc2.h"
#include "cal/cal.h"

/* External references */
extern uint16_t PROC1_$AS_ID;
extern uint16_t PROC1_$CURRENT;
extern uint8_t PROC2_UID[];
extern void PROC1_$GET_CPUT8(clock_t *cpu_time);
extern void PROC2_$SIGNAL_OS(void *uid, void *sig_num, void *sig_code, status_$t *status);

/* CPU limit database */
#define CPU_LIMIT_DB_BASE   0xE29198
#define CPU_LIMIT_DB_ENTRY_SIZE 0x1C

/* VT queue array base */
#define VT_QUEUE_ARRAY_BASE 0xE2A494

/* Signal number for CPU limit exceeded */
#define SIGXCPU 24

void TIME_$SET_CPU_LIMIT(clock_t *limit, int8_t *relative, status_$t *status)
{
    clock_t cpu_clock;
    clock_t limit_clock;
    clock_t interval;
    time_queue_t *vt_queue;
    uint8_t *cpu_entry;
    int16_t as_offset;
    int8_t cmp_result;

    *status = status_$ok;

    /* Convert limit to clock format */
    limit_clock.high = limit->high;
    limit_clock.low = limit->low;

    /* Get current CPU time */
    PROC1_$GET_CPUT8(&cpu_clock);

    /* Zero interval (no repeat) */
    interval.high = 0;
    interval.low = 0;

    /* Calculate VT queue for current process */
    vt_queue = (time_queue_t *)(VT_QUEUE_ARRAY_BASE +
                                (PROC1_$CURRENT * 12) - 12);

    /* Get CPU limit entry */
    as_offset = PROC1_$AS_ID * CPU_LIMIT_DB_ENTRY_SIZE;
    cpu_entry = (uint8_t *)(CPU_LIMIT_DB_BASE + as_offset);

    /* Remove any existing CPU limit timer */
    TIME_$Q_REMOVE_ELEM(vt_queue, (time_queue_elem_t *)cpu_entry, status);

    /* If limit is zero, just clear it */
    if (limit->high == 0 && limit->low == 0) {
        *(uint32_t *)(cpu_entry + 0x0C) = 0;
        *(uint16_t *)(cpu_entry + 0x10) = 0;
        *status = status_$ok;
        return;
    }

    /* Check if limit is relative or absolute */
    if (*relative >= 0) {
        /* Absolute limit - check if already exceeded */
        cmp_result = (int8_t)SUB48(&limit_clock, &cpu_clock);
        if (cmp_result < 0) {
            /* Already exceeded - send signal now */
            void *uid = &PROC2_UID[PROC1_$AS_ID * 8];
            uint16_t sig_num = SIGXCPU;
            uint32_t sig_code = 0;
            PROC2_$SIGNAL_OS(uid, &sig_num, &sig_code, status);
            return;
        }
    }

    /* Schedule the CPU limit callback */
    TIME_$Q_ADD_CALLBACK(vt_queue,
                         &limit_clock,
                         (*relative >= 0) ? 1 : 0,  /* relative flag */
                         &cpu_clock,
                         TIME_$SET_CPU_LIMIT_CALLBACK,
                         (void *)(uintptr_t)PROC1_$AS_ID,
                         4,  /* flags */
                         &interval,
                         (time_queue_elem_t *)cpu_entry,
                         status);
}
