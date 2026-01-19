/*
 * SIO_$INIT - Initialize a serial I/O port
 *
 * Initializes an SIO descriptor for the specified port number.
 * This function sets up the serial port hardware, initializes data
 * structures, and registers handlers with the terminal subsystem.
 *
 * Port 1 (console) has special handling:
 *   - Uses keyboard/display handlers (KBD_$RCV, etc.)
 *   - Sets up DXM (Display X Manager) support
 *   - Additional terminal configuration
 *
 * Other ports (generic serial):
 *   - Uses TTY handlers
 *   - Optional crash handler support
 *
 * Data structure layout (per the decompiled code):
 *   - Base data at 0xe2c9f0 (TERM_$DATA)
 *   - SIO descriptors at offset 0xf78 + port * 0x78
 *   - Large per-line data at offset + port * 0x4dc
 *   - DTTE at offset 0x12a0 + TERM_$MAX_DTTE * 0x38
 *
 * TODO: This function is complex and calls several unidentified helper
 * functions. More analysis needed to fully understand the initialization
 * sequence.
 *
 * Helper functions called:
 *   - OS_TERM_INIT (0x00e32a60) - Initialize OS terminal
 *   - FUN_00e32b26 - Unknown init function
 *   - FUN_00e32bb8 - Unknown init function
 *   - FUN_00e32ab2 - Unknown init function
 *   - FUN_00e32b76 - Unknown init function (sets port type?)
 *   - TTY_$I_ENABLE_CRASH_FUNC - Enable crash key handler
 *
 * Original address: 0x00e32be0
 */

#include "sio/sio_internal.h"

/*
 * Base address for SIO/TERM data
 * Original address: 0xe2c9f0
 */
#define SIO_DATA_BASE   0xe2c9f0

/*
 * Offsets within the data structure
 */
#define OFFSET_SIO_DESC_BASE    0xf78   /* First SIO descriptor */
#define OFFSET_DTTE_BASE        0x12a0  /* DTTE array */
#define OFFSET_MAX_DTTE         0x1388  /* TERM_$MAX_DTTE counter */

/*
 * Sizes
 */
#define SIO_DESC_SIZE           0x78    /* 120 bytes per SIO descriptor */
#define DTTE_SIZE               0x38    /* 56 bytes per DTTE entry */
#define LINE_DATA_SIZE          0x4dc   /* 1244 bytes per line data */

/* External helper function declarations - these need further analysis */
extern void OS_TERM_INIT(void *param1, void *param2, void **param3,
                         void *param4, void **param5, void *param6);
extern void FUN_00e32b26(void *param1, void *param2, void **param3, void *param4);
extern void FUN_00e32bb8(void *param1, void *param2, void **param3, void **param4);
extern void FUN_00e32ab2(void *param1, void *param2, void *param3, void **param4,
                         void **param5, void *param6, uint32_t param7);
extern void FUN_00e32b76(void *param1, int16_t param2);

void SIO_$INIT(int16_t port_num, uint32_t param2, uint32_t param3,
               sio_desc_t **desc_ret, int8_t flags, status_$t *status_ret)
{
    uint8_t *base = (uint8_t *)SIO_DATA_BASE;
    int32_t sio_offset;
    int32_t line_data_offset;
    sio_desc_t *desc;
    int16_t *max_dtte;
    void *local_ptrs[2];

    *status_ret = status_$ok;

    /*
     * Calculate offsets for this port
     * SIO descriptor: base + 0xf78 + port * 0x78
     * Line data: base + port * 0x4dc
     */
    sio_offset = port_num * SIO_DESC_SIZE;
    line_data_offset = port_num * LINE_DATA_SIZE;

    /* Get pointer to TERM_$MAX_DTTE counter */
    max_dtte = (int16_t *)(base + OFFSET_MAX_DTTE);

    /* Get DTTE entry for this port */
    /* dtte = base + OFFSET_DTTE_BASE + (*max_dtte) * DTTE_SIZE */

    if (port_num == 1) {
        /*
         * Console port initialization
         * Sets up keyboard and display handlers
         */

        /* TODO: Console-specific initialization
         * The decompiled code shows calls to:
         * - OS_TERM_INIT with console-specific parameters
         * - FUN_00e32b26 for additional setup
         * - FUN_00e32bb8 for more configuration
         * - FUN_00e32ab2 for handler registration
         * - FUN_00e32b76 with port type 2 (console)
         */

        /* Placeholder - actual implementation needs helper functions */
        desc = (sio_desc_t *)(base + OFFSET_SIO_DESC_BASE + sio_offset);

    } else {
        /*
         * Generic serial port initialization
         */

        /* TODO: Generic port initialization
         * - FUN_00e32b26 setup
         * - FUN_00e32ab2 handler registration
         * - FUN_00e32b76 with port type 0 (serial)
         */

        desc = (sio_desc_t *)(base + OFFSET_SIO_DESC_BASE + sio_offset);

        if (flags < 0) {
            /*
             * Enable crash handler - allows special key sequence
             * to trigger system crash/debug
             * Key code 0x1B (ESC), flags 0xFF
             */
            TTY_$I_ENABLE_CRASH_FUNC(
                (void *)(base + line_data_offset - 0x384),
                0x1B,
                0xFF
            );
            *status_ret = status_$ok;
        } else {
            /*
             * Normal initialization - call driver init function
             * The decompiled code shows an indirect call through
             * a function pointer at offset 0xfb8 in the descriptor area
             */
            /* Placeholder for driver-specific initialization */
        }
    }

    /* Return the SIO descriptor pointer */
    *desc_ret = desc;

    /* Increment TERM_$MAX_DTTE counter */
    (*max_dtte)++;
}
