/*
 * PROC2_$STARTUP - Process startup
 *
 * Called to complete process startup after creation.
 * Performs the following steps:
 * 1. Sets the address space ID (ASID) for the process
 * 2. Clears superuser mode
 * 3. Marks the process as valid
 * 4. Calls FIM startup handler
 *
 * Parameters:
 *   context - Startup context structure containing:
 *             - offset 0x08: ASID (address space ID)
 *
 * Original address: 0x00e73454
 */

#include "proc2.h"

/* External functions */
extern void ACL_$CLEAR_SUPER(void);
extern void FIM_$PROC2_STARTUP(void *context);

/*
 * Startup context structure
 * The full structure size is unknown, but offset 0x08 contains the ASID.
 */
typedef struct startup_context_t {
    uint8_t     pad_00[8];      /* 0x00: Unknown */
    uint16_t    asid;           /* 0x08: Address space ID */
    /* Additional fields unknown */
} startup_context_t;

void PROC2_$STARTUP(void *context)
{
    startup_context_t *ctx = (startup_context_t *)context;

    /* Set the address space ID for this process */
    PROC1_$SET_ASID(ctx->asid);

    /* Clear superuser mode */
    ACL_$CLEAR_SUPER();

    /* Mark the process as valid */
    PROC2_$SET_VALID();

    /* Call FIM startup handler to complete initialization */
    FIM_$PROC2_STARTUP(context);
}
