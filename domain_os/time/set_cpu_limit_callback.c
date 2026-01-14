/*
 * TIME_$SET_CPU_LIMIT_CALLBACK - Callback for CPU limit timer
 *
 * Called when a process exceeds its CPU time limit.
 * Sends SIGXCPU to the process.
 *
 * Parameters:
 *   arg - Pointer to callback argument containing AS info
 *
 * Original address: 0x00e58af8
 */

#include "time.h"
#include "proc2/proc2.h"

/* Signal numbers */
#define SIGXCPU 24

/* CPU limit database base (same structure as itimer but different base) */
#define CPU_LIMIT_DB_BASE   0xE29198
#define CPU_LIMIT_DB_ENTRY_SIZE 0x1C

/* Offsets */
#define CPU_LIMIT_HIGH      0x0C
#define CPU_LIMIT_LOW       0x10

/* Process UID array */
extern uint8_t PROC2_UID[];

/* External signal function */
extern void PROC2_$SIGNAL_OS(void *uid, void *sig_num, void *sig_code, status_$t *status);

void TIME_$SET_CPU_LIMIT_CALLBACK(void *arg)
{
    uint32_t **arg_ptr = (uint32_t **)arg;
    uint32_t *inner = *arg_ptr;
    uint16_t as_id = (uint16_t)inner[0];
    int16_t as_offset;
    uint8_t *cpu_entry;
    uint32_t limit_high;
    uint16_t limit_low;
    status_$t status;

    /* Get the CPU limit entry for this AS */
    as_offset = as_id * CPU_LIMIT_DB_ENTRY_SIZE;
    cpu_entry = (uint8_t *)(CPU_LIMIT_DB_BASE + as_offset);

    /* Check if there's a limit set */
    limit_high = *(uint32_t *)(cpu_entry + CPU_LIMIT_HIGH);
    limit_low = *(uint16_t *)(cpu_entry + CPU_LIMIT_LOW);

    if (limit_high != 0 || limit_low != 0) {
        /* Send SIGXCPU to the process */
        void *uid = &PROC2_UID[as_id * 8];
        uint16_t sig_num = SIGXCPU;
        uint32_t sig_code = 0;
        PROC2_$SIGNAL_OS(uid, &sig_num, &sig_code, &status);
    }
}
