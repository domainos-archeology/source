/*
 * XPD_$INIT - Initialize the XPD (eXtended Process Debugging) subsystem
 *
 * This function initializes the XPD data area by:
 * 1. Wiring the XPD and PROC2 data areas into memory
 * 2. Zeroing the XPD data area (0x4E8 bytes)
 * 3. Initializing eventcounts for all 57 process slots (0x14 bytes apart)
 * 4. Initializing eventcounts for debugger table slots (6 entries at 0x478 offset)
 *
 * Original address: 0x00e32304
 */

#include "xpd/xpd.h"
#include "mst/mst.h"
#include "os/os.h"

/*
 * External data references
 *
 * XPD_$DATA starts at 0xEA5034 and contains:
 *   - 57 eventcounts at 0x14 byte intervals (for target processes)
 *   - 6 debugger eventcounts at offset 0x478 (for debugger slots)
 */
extern void *PTR_XPD_$DATA;     /* 0x00e32390: Pointer to XPD data area */
extern void *PTR_PROC2_$DATA;   /* 0x00e3238c: Pointer to PROC2 data area */

void XPD_$INIT(void)
{
    int16_t i;
    ec_$eventcount_t *ec;
    uint8_t wire_params1[16];
    uint8_t wire_params2[2];

    /* Wire the XPD and PROC2 data areas into physical memory */
    MST_$WIRE_AREA(&PTR_XPD_$DATA, &PTR_PROC2_$DATA, wire_params1,
                   (void *)0x00e3238a, wire_params2);

    /* Zero the entire XPD data area (0x4E8 = 1256 bytes) */
    OS_$DATA_ZERO(&XPD_$DATA, 0x4E8);

    /*
     * Initialize eventcounts for all 57 process debug slots
     * These are spaced 0x14 (20) bytes apart, starting at XPD_$DATA
     * Each process slot has an eventcount used for debugger notification
     */
    ec = (ec_$eventcount_t *)&XPD_$DATA;
    for (i = 0; i < 58; i++) {  /* 0x39 + 1 = 58 iterations (0-57) */
        EC_$INIT(ec);
        /* Move to next eventcount (0x14 bytes = 20 bytes) */
        ec = (ec_$eventcount_t *)((char *)ec + 0x14);
    }

    /*
     * Initialize eventcounts for debugger table slots (6 entries)
     * These are at offset 0x478 from XPD_$DATA, spaced 0x10 bytes apart
     * Each debugger slot has an eventcount for event notification
     */
    ec = (ec_$eventcount_t *)((char *)&XPD_$DATA + 0x478);
    for (i = 0; i < 6; i++) {   /* 0x5 + 1 = 6 iterations (0-5) */
        EC_$INIT(ec);
        /* Move to next eventcount (0x10 bytes = 16 bytes) */
        ec = (ec_$eventcount_t *)((char *)ec + 0x10);
    }
}
