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
 * 6. Calls ML_$EXCLUSION_INIT on the tracking-rect exclusion lock (0x00E2E520)
 *
 * The function uses a jump table at 0x00E34D62 for different initialization
 * paths based on the number of displays. In this decompilation, we handle
 * the common case of initializing available displays.
 */

#include "smd/smd_internal.h"
#include "prom/prom.h"

/*
 * Controller type word passed by reference to io_$probe (0x00E29138).
 * Original: pea (0xdc,PC) -> 0x00E34EB0, which holds the 16-bit constant 1.
 */
static uint16_t smd_probe_type = 1;

/*
 * TODO: SMD_$INIT dispatches through a jump table at 0x00E34D62 on a value
 * that is always 0 (clr.w D0), i.e. it always executes the case-0 prologue at
 * 0x00E34D6A before falling into the per-unit loop at 0x00E34DB2.  That
 * prologue (not yet decompiled) stores:
 *   unit[1]+0x00  = &SMD_GLOBALS + 0x1748
 *   unit[1]+0x04  = &SMD_GLOBALS + 0x1788
 *   unit[1]+0x14  = 0x00FC0000
 *   unit[1]+0x08  = 0x00FF9800
 *   unit[0]+0x18  = &SMD_DISPLAY_INFO[0] (0x00E27376), then clears its word 0
 *   unit[1]+0x0C/+0x10 = 8 bytes copied from 0x00E173D4
 * (offsets relative to 0x00E2E3FC).  This needs to be added here.
 */

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
 * 5. Initialize the tracking-rect exclusion lock
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
    /*
     * Original: movea.l #0xe2e3fc,A2; lea (0x10c,A2),A2.  0x00E2E3FC is the
     * base of SMD_DISPLAY_UNITS (SMD_EC_1 is its first 12 bytes), so this is
     * &SMD_DISPLAY_UNITS[1]; slot N-1 holds the hw pointer used below.
     */
    unit_base = (smd_display_unit_t *)((uint8_t *)SMD_DISPLAY_UNITS + SMD_DISPLAY_UNIT_SIZE);

    /* Initialize display units */
    do {
        /* Original: movea.l (-0xf4,A3),A4 - loads the hw pointer stored at
         * unit_base - 0xF4 (= previous slot's +0x18 field). */
        hw = ((smd_display_unit_t *)((uint8_t *)unit_base - SMD_DISPLAY_UNIT_SIZE))->hw;

        /* Clear mapped address count */
        /* TODO: original is clr.l (-0xf0,A3), i.e. the 32-bit field at
         * (unit_base - 0xF0) = previous slot +0x1C, not this slot's field_10. */
        unit_base->field_10 = 0;

        /* Probe for display hardware.
         * Original: pea (-0x4,A6); pea (0x8,A3); pea (0xdc,PC); jsr io_$probe
         * i.e. io_$probe(&type_word, unit_base + 8, &probe_result). */
        probe_status = io_$probe(
            &smd_probe_type,
            (void *)((uint8_t *)unit_base + 8),
            probe_result
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
        /* TODO: original is clr.w (-0xea,A3), i.e. the 16-bit word at
         * (unit_base - 0xEA) = previous slot +0x22, not this slot's field_16. */
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
        /* Original: move.l #0x10000,(0x22,A4) - one 32-bit store covering
         * video_flags (0x22) and field_24 (0x24): 0x0001 then 0x0000. */
        hw->video_flags = 0x0001;  /* Initial video state */
        hw->field_24 = 0x0000;
        hw->field_3e = 0;
        hw->field_5e = 0;

        /* Initialize cursor event count */
        EC_$INIT(&hw->cursor_ec);

        /* Clear field_4c */
        hw->field_4c = 0;

        /* Clear mapped address table (58 entries, 4 bytes each) */
        /* TODO: original loop is moveq #0x38,D0 / clr.l (-0xe8,A3+D1) with D1
         * starting at 4: 57 longs at (unit_base - 0xE4) .. (unit_base + 0x08),
         * i.e. previous slot +0x28 .. +0x108, not this slot +4 .. +0xEC. */
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

    /* Initialise the tracking-rectangle / cursor exclusion lock.
     * Original: pea (0xe2e520).l; jsr ML_$EXCLUSION_INIT (0x00E20E34). */
    ML_$EXCLUSION_INIT(&ml_$exclusion_t_00e2e520);
}
