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

    /* The SIO_$INIT_* / OS_TERM_INIT / SIO6509_$INIT / SIO2681_$INIT helpers
     * take pointers to 32-bit address cells (m68k_ptr_t *).  local_vars[] holds
     * those cells; on the m68k a void* is the same 32-bit word, so the casts
     * below only change the C type, not the value passed. */
    OS_TERM_INIT((uint32_t *)DAT_00e2db58, (uint32_t *)DTTE, (uint32_t *)&local_vars[3],
                 (uint32_t *)&PTR_TTY_$I_RCV_00e2cab0, (uint32_t *)&local_vars[0],
                 (uint32_t *)DAT_00e2caa0);

    SIO_$INIT_LINE(DAT_00e2cb48, local_vars[2], (m68k_ptr_t *)&local_vars[1], DAT_00e2ca60);

    local_vars[3] = DAT_00e2cb48;
    local_vars[4] = DAT_00e2cf1a;
    SIO_$INIT_DRAIN_HANDLER((m68k_ptr_t *)DAT_00e2db48, local_vars[2],
                            (m68k_ptr_t *)&local_vars[4], (m68k_ptr_t *)&local_vars[3]);

    local_vars[5] = DAT_00e2db58;
    local_vars[4] = DAT_00e2dc40;
    local_vars[6] = DAT_00e2dbf6;
    SIO_$INIT_DESC((sio_desc_t *)local_vars[0], DAT_00e2ca48, local_vars[2],
                   (m68k_ptr_t *)&local_vars[5], (m68k_ptr_t *)&local_vars[6],
                   (m68k_ptr_t *)&PTR_KBD_$RCV_00e2ca78, (m68k_ptr_t *)&local_vars[4],
                   (char *)DAT_00e35154);

    SIO_$INIT_DTTE((dtte_t *)local_vars[2], 2);

    // Initialize SIO 6509 (keyboard/display controller)
    // 0xe33018: pea 0xe351ae ; pea (-0x14,A6) ; pea 0xe2dc40 ; pea 0xe3321e ; pea 0xe33220
    SIO6509_$INIT(&DAT_00e33220, &DAT_00e3321e, DAT_00e2dc40,
                  (m68k_ptr_t *)&local_vars[0], DAT_00e351ae);

    // Initialize serial line 1
    local_vars[2] = DAT_00e2dcc8;
    local_vars[0] = DAT_00e2da58;
    SIO_$INIT_LINE(DAT_00e2d024, DAT_00e2dcc8, (m68k_ptr_t *)&local_vars[0], DAT_00e2ca30);

    local_vars[3] = DAT_00e2d024;
    local_vars[6] = &TONE_$CHANNEL;
    local_vars[4] = DAT_00e2d3f6;  // Note: address not in extern list
    SIO_$INIT_DESC((sio_desc_t *)DAT_00e2da58, DAT_00e2c9f0, local_vars[2],
                   (m68k_ptr_t *)&local_vars[3], (m68k_ptr_t *)&local_vars[4],
                   (m68k_ptr_t *)&PTR_TTY_$I_RCV_00e2ca08, (m68k_ptr_t *)&local_vars[6],
                   (char *)DAT_00e3517c);

    SIO_$INIT_DTTE((dtte_t *)local_vars[2], 0);

    // Initialize serial line 2
    local_vars[2] = DAT_00e2dd00;
    local_vars[0] = DAT_00e2dad0;
    SIO_$INIT_LINE(DAT_00e2d500, DAT_00e2dd00, (m68k_ptr_t *)&local_vars[0], DAT_00e2ca30);

    local_vars[3] = DAT_00e2d500;
    local_vars[4] = DAT_00e2dc74;
    local_vars[6] = DAT_00e2d8d2;
    SIO_$INIT_DESC((sio_desc_t *)DAT_00e2dad0, DAT_00e2c9f0, local_vars[2],
                   (m68k_ptr_t *)&local_vars[3], (m68k_ptr_t *)&local_vars[6],
                   (m68k_ptr_t *)&PTR_TTY_$I_RCV_00e2ca08, (m68k_ptr_t *)&local_vars[4],
                   (char *)DAT_00e3517c);

    SIO_$INIT_DTTE((dtte_t *)local_vars[2], 0);

    // Special initialization for process 1
    if (*param1 == 1) {
        int offset = (short)(*param2 * 0x78);  // 0x78 = 120 bytes per entry
        *(unsigned long *)(DAT_00e2da38 + offset) = init_value;
    }

    // Initialize SIO 2681 (dual UART)
    // 0xe3319e-0xe331ca pushes ten arguments (lea 0x28,SP after the call):
    //   config=0xe351a0 (0x4c,A5), chip=0xe2dc48, chan_b_params=0xe2db1c,
    //   &chan_b_desc, chan_b=0xe2dc74, chan_a_params=0xe2daa4, &chan_a_desc,
    //   chan_a=0xe2dc58, chip_num=0xe33220, int_vec=0xe33220 (move.l (SP),-(SP))
    local_vars[7] = DAT_00e2dad0;
    local_vars[8] = DAT_00e2da58;
    SIO2681_$INIT(&DAT_00e33220, &DAT_00e33220,
                  &TONE_$CHANNEL, (sio_desc_t **)&local_vars[8],
                  (sio_params_t *)DAT_00e2daa4,
                  (sio2681_channel_t *)DAT_00e2dc74, (sio_desc_t **)&local_vars[7],
                  (sio_params_t *)DAT_00e2db1c,
                  (sio2681_chip_t *)DAT_00e2dc48, DAT_00e351a0);

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
