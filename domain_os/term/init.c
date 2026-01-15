#include "term/term_internal.h"

// Initializes the terminal subsystem.
//
// This is a complex initialization function that sets up:
// - DTTE (Display Terminal Table Entry) structures
// - TTY handlers and callbacks
// - SIO (Serial I/O) hardware initialization
// - Keyboard handlers
// - SUMA (Screen Update Manager?) initialization
//
// The two parameters appear to relate to process/hardware configuration:
//   param1: pointer to a flag (1 = special initialization mode)
//   param2: pointer to terminal line number
void TERM_$INIT(short *param1, short *param2) {
    short i;
    unsigned long init_value = 0xFFFFFFFF;
    void *local_vars[16];  // Stack frame for various pointers

    // Initialize DTTE entries (clear handler pointers)
    // There are 4 entries (indices 0-3), each 0x38 bytes
    for (i = 3; i >= 0; i--) {
        TERM_$DATA.dtte[i].handler_ptr = 0;
        TERM_$DATA.dtte[i].alt_handler = 0;
        TERM_$DATA.dtte[i].tty_handler = 0;
        TERM_$DATA.dtte[i].ptr_30 = 0;
    }

    // Initialize terminal 0 (display terminal)
    local_vars[0] = DAT_00e2d9e0;
    local_vars[1] = DAT_00e2db48;
    local_vars[2] = DTTE;
    local_vars[3] = DAT_00e2cb48;

    OS_TERM_INIT(DAT_00e2db58, DTTE, (void **)&local_vars[3],
                 &PTR_TTY_$I_RCV_00e2cab0, (void **)&local_vars[0], DAT_00e2caa0);

    FUN_00e32b26(DAT_00e2cb48, local_vars[2], (void **)&local_vars[1], DAT_00e2ca60);

    local_vars[3] = DAT_00e2cb48;
    local_vars[4] = DAT_00e2cf1a;
    FUN_00e32bb8(DAT_00e2db48, local_vars[2], (void **)&local_vars[4], (void **)&local_vars[3]);

    local_vars[5] = DAT_00e2db58;
    local_vars[4] = DAT_00e2dc40;
    local_vars[6] = DAT_00e2dbf6;
    FUN_00e32ab2(local_vars[0], DAT_00e2ca48, local_vars[2],
                 (void **)&local_vars[5], (void **)&local_vars[6],
                 &PTR_KBD_$RCV_00e2ca78, (void **)&local_vars[4]);

    FUN_00e32b76(local_vars[2], 2);

    // Initialize SIO 6509 (keyboard/display controller)
    SIO6509_$INIT(&DAT_00e33220, &DAT_00e3321e, DAT_00e2dc40,
                  (void **)&local_vars[0], &DAT_00e351ae);

    // Initialize serial line 1
    local_vars[2] = DAT_00e2dcc8;
    local_vars[0] = DAT_00e2da58;
    FUN_00e32b26(DAT_00e2d024, DAT_00e2dcc8, (void **)&local_vars[0], DAT_00e2ca30);

    local_vars[3] = DAT_00e2d024;
    local_vars[6] = DAT_00e2dc58;
    local_vars[4] = DAT_00e2d3f6;  // Note: address not in extern list
    FUN_00e32ab2(DAT_00e2da58, DAT_00e2c9f0, local_vars[2],
                 (void **)&local_vars[3], (void **)&local_vars[4],
                 &PTR_TTY_$I_RCV_00e2ca08, (void **)&local_vars[6]);

    FUN_00e32b76(local_vars[2], 0);

    // Initialize serial line 2
    local_vars[2] = DAT_00e2dd00;
    local_vars[0] = DAT_00e2dad0;
    FUN_00e32b26(DAT_00e2d500, DAT_00e2dd00, (void **)&local_vars[0], DAT_00e2ca30);

    local_vars[3] = DAT_00e2d500;
    local_vars[4] = DAT_00e2dc74;
    local_vars[6] = DAT_00e2d8d2;
    FUN_00e32ab2(DAT_00e2dad0, DAT_00e2c9f0, local_vars[2],
                 (void **)&local_vars[3], (void **)&local_vars[6],
                 &PTR_TTY_$I_RCV_00e2ca08, (void **)&local_vars[4]);

    FUN_00e32b76(local_vars[2], 0);

    // Special initialization for process 1
    if (*param1 == 1) {
        int offset = (short)(*param2 * 0x78);  // 0x78 = 120 bytes per entry
        *(unsigned long *)(DAT_00e2da38 + offset) = init_value;
    }

    // Initialize SIO 2681 (dual UART)
    local_vars[7] = DAT_00e2dad0;
    local_vars[8] = DAT_00e2da58;
    SIO2681_$INIT(&DAT_00e33220, &DAT_00e33220, DAT_00e2dc58,
                  (void **)&local_vars[8], DAT_00e2daa4, DAT_00e2dc74,
                  (void **)&local_vars[7], DAT_00e2db1c, DAT_00e2dc48);

    // Enable crash handler for process 1
    if (*param1 == 1) {
        int offset = (short)(*param2 * sizeof(dtte_t));
        void *handler = *(void **)(DAT_00e2dcb4 + offset);
        // Enable ESC (0x1b) as crash key with mask 0xff
        TTY_$I_ENABLE_CRASH_FUNC(handler, 0x1b, 0xff);
    }

    // Set maximum DTTE count
    TERM_$DATA.max_dtte = 3;

    // Initialize SUMA (Screen Update Manager?)
    SUMA_$INIT();
}
