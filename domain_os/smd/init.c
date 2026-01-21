/*
 * smd/init.c - SMD_$INIT implementation
 *
 * Full initialization of the SMD (Screen Management Display) subsystem.
 *
 * Original address: 0x00E34D2C
 *
 * Assembly analysis:
 * This is a complex initialization function that:
 * 1. Initializes global event counts (SMD_EC_1 and SMD_EC_2)
 * 2. Iterates through display units (based on a switch table)
 * 3. For each display unit:
 *    - Probes for display hardware
 *    - Initializes the display unit structure
 *    - Sets display dimensions based on type
 *    - Initializes multiple event counts
 *    - Clears mapped address table
 * 4. Calls SMD_$INTERRUPT_INIT to set up interrupt handlers
 * 5. Initializes global state variables
 * 6. Calls CAL_$SETUP_CALLBACK for blink timer setup
 *
 * The function uses a jump table at 0x00E34D62 for different initialization
 * paths based on the number of displays. In this decompilation, we handle
 * the common case of initializing available displays.
 */

#include "smd/smd_internal.h"
#include "cal/cal.h"

/* Display probe function - returns display type or 0 */
extern int8_t SMD_$PROBE_DISPLAY(uint8_t *result, void *hw_base, void *callback);

/* Callback for display probe completion */
extern void SMD_$PROBE_CALLBACK(void);

/* Global state initialization base at 0x00E82B8C */
extern uint8_t SMD_GLOBAL_STATE_BASE[];

/* Callback queue for blink timer at 0x00E2E520 */
extern void *SMD_BLINK_CALLBACK_QUEUE;

/*
 * SMD_$INIT - Initialize SMD subsystem
 *
 * Performs complete initialization of the display management subsystem.
 * Called during system startup.
 *
 * Initialization sequence:
 * 1. Initialize global event counts
 * 2. For each display unit:
 *    - Probe for hardware presence
 *    - Initialize unit structure
 *    - Set up event counts
 *    - Configure display dimensions
 * 3. Set up interrupt handlers
 * 4. Initialize global state
 * 5. Register blink timer callback
 */
void SMD_$INIT(void)
{
    int16_t unit_index;
    int32_t unit_offset;
    smd_display_unit_t *unit_base;
    smd_display_hw_t *hw;
    uint8_t probe_result[4];
    int8_t probe_status;
    uint16_t disp_type;

    /* Initialize global event counts */
    EC_$INIT(&SMD_EC_1);
    EC_$INIT(&SMD_EC_2);

    /*
     * The original code uses a switch/jump table here for different
     * initialization paths. The common path (case 0-3) falls through
     * to initialize displays. We implement the display loop directly.
     */

    /* Start with display unit 0 */
    unit_index = 0;
    unit_base = (smd_display_unit_t *)((uint8_t *)&SMD_EC_1 + SMD_DISPLAY_UNIT_SIZE);

    /* Initialize display units */
    do {
        hw = (smd_display_hw_t *)((uint8_t *)unit_base - 0xF4);

        /* Clear mapped address count */
        unit_base->field_10 = 0;

        /* Probe for display hardware */
        probe_status = SMD_$PROBE_DISPLAY(
            probe_result,
            (void *)((uint8_t *)unit_base + 8),
            SMD_$PROBE_CALLBACK
        );

        if (probe_status < 0) {
            /* Probe failed - mark display as type 2 (default) */
            hw->display_type = 2;
        }

        /* Clear field_52 and field_4e */
        hw->field_52 = 0;
        hw->field_4e = 0;

        /* Get display type and set dimensions */
        disp_type = hw->display_type;

        if (disp_type == SMD_DISP_TYPE_MONO_LANDSCAPE) {
            /* 1024x800 landscape */
            hw->width = 0x3FF;   /* 1023 */
            hw->height = 0x31F;  /* 799 */
        } else if (disp_type == SMD_DISP_TYPE_MONO_PORTRAIT) {
            /* 800x1024 portrait */
            hw->width = 0x31F;   /* 799 */
            hw->height = 0x3FF;  /* 1023 */
        }
        /* Other display types keep default dimensions */

        /* Clear unit event count index */
        unit_base->field_16 = 0;

        /* Clear lock state */
        hw->lock_state = 0;

        /* Initialize lock event count */
        EC_$INIT(&hw->lock_ec);

        /* Initialize operation event count */
        EC_$INIT(&hw->op_ec);

        /* Clear various hardware state fields */
        hw->field_1c = 0;
        hw->tracking_enabled = 0;
        hw->field_20 = 0;
        hw->video_flags = 0x00010000;  /* Initial video state */
        hw->field_3e = 0;
        hw->field_5e = 0;

        /* Initialize cursor event count */
        EC_$INIT(&hw->cursor_ec);

        /* Clear field_4c */
        hw->field_4c = 0;

        /* Clear mapped address table (58 entries, 4 bytes each) */
        {
            int16_t i;
            uint32_t *addr_table = (uint32_t *)((uint8_t *)unit_base + 4);
            for (i = 0; i < 58; i++) {
                addr_table[i] = 0;
            }
        }

        /* Move to next unit */
        unit_base = (smd_display_unit_t *)((uint8_t *)unit_base + SMD_DISPLAY_UNIT_SIZE);
        unit_index--;
    } while (unit_index >= 0);

    /* Initialize interrupt handlers */
    SMD_$INTERRUPT_INIT();

    /* Initialize global state at 0x00E82B8C */
    {
        uint8_t *global_state = (uint8_t *)&SMD_GLOBALS;

        /* Clear blank_time at offset 0xC8 */
        *(uint32_t *)(global_state + 0xC8) = 0;

        /* Set blank_timeout at offset 0xD8 to 3433 (default timeout) */
        *(uint32_t *)(global_state + 0xD8) = 0xD69;  /* 3433 */
    }

    /* Register blink timer callback */
    CAL_$SETUP_CALLBACK(&SMD_BLINK_CALLBACK_QUEUE);
}
