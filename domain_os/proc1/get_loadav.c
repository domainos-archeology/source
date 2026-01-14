/*
 * PROC1_$GET_LOADAV - Get system load averages
 * Original address: 0x00e14bba
 *
 * Returns the three load average values (1, 5, and 15 minute averages).
 * These are stored as fixed-point numbers.
 *
 * Parameters:
 *   loadav - Pointer to receive 3 uint32_t load averages
 */

#include "proc1.h"

/*
 * Load average storage (12 bytes - three 32-bit values)
 * Base address: 0xe254e8
 */
#if defined(M68K)
    #define LOADAV_1MIN     (*(uint32_t*)0xe254e8)
    #define LOADAV_5MIN     (*(uint32_t*)0xe254ec)
    #define LOADAV_15MIN    (*(uint32_t*)0xe254f0)
#else
    extern uint32_t LOADAV_1MIN;
    extern uint32_t LOADAV_5MIN;
    extern uint32_t LOADAV_15MIN;
#endif

void PROC1_$GET_LOADAV(uint32_t *loadav)
{
    loadav[0] = LOADAV_1MIN;
    loadav[1] = LOADAV_5MIN;
    loadav[2] = LOADAV_15MIN;
}
