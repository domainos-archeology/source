/*
 * PKT_$INIT - Initialize the packet module
 *
 * Initializes the PKT subsystem and creates the ping server process.
 * Must be called during system initialization.
 *
 * Original address: 0x00E2F84C
 */

#include "pkt/pkt_internal.h"
#include "misc/crash_system.h"

/*
 * Process type for ping server:
 * Low byte (0x0F): Stack type
 * High bytes (0x800000): Process type flags
 */
#define PING_SERVER_TYPE    0x0800000F

void PKT_$INIT(void)
{
    status_$t status;

    /* Create the ping server process */
    PROC1_$CREATE_P(PKT_$PING_SERVER, PING_SERVER_TYPE, &status);

    if (status != status_$ok) {
        CRASH_SYSTEM(&status);
    }
}
