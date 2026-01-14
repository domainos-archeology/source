/*
 * PROC2_$GET_CPU_USAGE - Get CPU usage for current process
 *
 * Returns CPU usage data from PROC1 with a constant appended.
 * Output is 20 bytes (5 longwords): 4 from PROC1 + constant 0x411c.
 *
 * Parameters:
 *   usage - Pointer to receive usage data (5 longwords = 20 bytes)
 *
 * Original address: 0x00e41d2a
 */

#include "proc2.h"

/* Note: PROC1_$GET_CPU_USAGE actually takes 3 pointer params, not 2 as declared
 * in proc1.h. The correct signature is:
 *   void PROC1_$GET_CPU_USAGE(uint32_t *time_data, uint32_t *extra1, uint32_t *extra2);
 * where time_data receives 6 bytes, extra1 and extra2 each receive 4 bytes.
 */
typedef void (*proc1_get_cpu_usage_t)(uint32_t *, uint32_t *, uint32_t *);

void PROC2_$GET_CPU_USAGE(uint32_t *usage)
{
    uint32_t local_data[6];  /* Local buffer: first 6 bytes from PROC1 */
    uint32_t extra1;         /* 4 bytes from PROC1 */
    uint32_t extra2;         /* 4 bytes from PROC1 */
    int i;
    proc1_get_cpu_usage_t get_cpu_usage;

    /* Cast to correct signature */
    get_cpu_usage = (proc1_get_cpu_usage_t)PROC1_$GET_CPU_USAGE;

    /* Get CPU usage from PROC1 */
    get_cpu_usage(local_data, &extra1, &extra2);

    /* Copy first 5 longwords (20 bytes) to output */
    for (i = 0; i < 5; i++) {
        usage[i] = local_data[i];
    }

    /* Overwrite 5th element with constant */
    usage[4] = 0x411c;
}
