/*
 * SIO_$INIT - Initialize a serial I/O port
 *
 * Initializes an SIO descriptor for the specified port number.
 * This function sets up the serial port hardware, initializes data
 * structures, and registers handlers with the terminal subsystem.
 *
 * Port 1 (console) has special handling:
 *   - Calls OS_TERM_INIT to set up keyboard/display terminal structure
 *   - Uses keyboard handlers (KBD_$RCV, KBD_$OUTPUT_BUFFER_DRAINED)
 *   - Sets up drain notification handler
 *   - Uses console parameter template
 *   - Discipline 2 (console mode)
 *
 * Other ports (generic serial):
 *   - Uses TTY handlers (TTY_$I_RCV, etc.)
 *   - Uses generic parameter template
 *   - Discipline 0 (serial TTY mode)
 *   - Optional crash handler support (flags bit 7)
 *   - Calls driver set_params for initial configuration
 *
 * Data structure layout within TERM_$DATA (base = 0xe2c9f0):
 *   - SIO descriptors at base + 0xf78 + port * 0x78
 *   - Per-line TTY data at base + port * 0x4dc - 0x384
 *   - DTTE array at base + 0x12a0 + TERM_$MAX_DTTE * 0x38
 *   - Console terminal data at base + port * 0xe4 + 0x1084
 *   - Drain handler records at base + port * 0xc + 0x114c
 *   - TERM_$MAX_DTTE counter at base + 0x1388
 *
 * Assembly register mapping:
 *   D2 = port_num
 *   D3 = flags (also reused as port * 0x4dc in console path)
 *   D4 = base + port * 0x78 (saved for descriptor address)
 *   D5 = context_ptr (passed through to SIO_$INIT_DESC)
 *   D6 = vtable_ptr (passed through to SIO_$INIT_DESC)
 *   A4 = status_ret (in else branch; reused in console branch)
 *
 * Stack frame: link -0xc (3 longword locals for pass-by-reference)
 *   FP-0x08: temporary pointer for pass-by-reference params
 *   FP-0x0c: temporary pointer for pass-by-reference params
 *
 * Original address: 0x00e32be0
 * Size: 730 bytes
 */

#include "sio/sio_internal.h"

void SIO_$INIT(int16_t port_num, void *context_ptr, void *vtable_ptr,
               sio_desc_t **desc_ret, int8_t flags, status_$t *status_ret)
{
    uint8_t *base = (uint8_t *)&TERM_$DATA;
    dtte_t *dtte;

    /*
     * Two local variables used as pass-by-reference temporaries.
     * In the assembly, these are stack locals at FP-0x08 and FP-0x0c.
     * Functions like SIO_$INIT_DESC take m68k_ptr_t* params and dereference
     * them to get 32-bit address values. These locals hold those values.
     *
     * NOTE: On a 64-bit host, casting a pointer to m68k_ptr_t (uint32_t)
     * truncates the address. This is correct for m68k semantics but won't
     * work for host-native execution without an address translation layer.
     */
    m68k_ptr_t local_8;     /* Corresponds to (FP-0x08) in assembly */
    m68k_ptr_t local_c;     /* Corresponds to (FP-0x0c) in assembly */

    /* Clear status (assembly: clr.l (A4) at 0xe32bfc) */
    *status_ret = status_$ok;

    if (port_num == 1) {
        /*
         * ================================================================
         * Console port initialization (port 1 = keyboard/display)
         * ================================================================
         *
         * The console uses specialized KBD handlers and a DXM terminal
         * structure. The initialization sequence:
         *   1. OS_TERM_INIT - set up console terminal structure
         *   2. SIO_$INIT_LINE - initialize TTY line descriptor
         *   3. SIO_$INIT_DRAIN_HANDLER - set up drain notification
         *   4. SIO_$INIT_DESC - populate SIO descriptor
         *   5. SIO_$INIT_DTTE - initialize DTTE with discipline=2
         */

        /* Precompute per-port offsets */
        int32_t desc_offset = (int32_t)port_num * SIO_DESC_STRIDE;
        int32_t line_offset = (int32_t)port_num * SIO_LINE_DATA_STRIDE;
        int32_t console_offset = (int32_t)port_num * SIO_CONSOLE_PORT_STRIDE;
        int32_t drain_offset = (int32_t)port_num * SIO_DRAIN_HANDLER_STRIDE;

        /*
         * Step 1: OS_TERM_INIT - Initialize console terminal structure
         *
         * Sets up the console terminal data at base + console_offset + 0x1084
         * with handler pointers, SIO descriptor reference, and line data reference.
         *
         * Assembly at 0xe32c06-0xe32c74:
         *   local_c = base + line_offset - 0x384 (line data address)
         *   local_8 = base + desc_offset + 0xf78 (SIO descriptor address)
         *   Push: base+0xb0, &local_8, base+0xc0, &local_c, DTTE, term_data
         */
        local_c = (m68k_ptr_t)(uintptr_t)(base + line_offset - SIO_LINE_DATA_ADJUST);
        local_8 = (m68k_ptr_t)(uintptr_t)(base + desc_offset + SIO_DESC_BASE_OFFSET);

        dtte = &TERM_$DATA.dtte[TERM_$MAX_DTTE];

        OS_TERM_INIT(
            base + console_offset + SIO_CONSOLE_TERM_OFFSET,    /* console term data */
            (void *)dtte,                                       /* DTTE entry */
            &local_c,                                           /* &(line data addr) */
            (m68k_ptr_t *)(base + SIO_CONSOLE_I_RCV_OFFSET),   /* &PTR_TTY_$I_RCV */
            &local_8,                                           /* &(SIO desc addr) */
            base + SIO_CONSOLE_VTABLE_OFFSET                    /* console vtable */
        );

        /*
         * Step 2: SIO_$INIT_LINE - Initialize TTY line descriptor
         *
         * For console: line ID comes from drain handler area address.
         *
         * Assembly at 0xe32c7c-0xe32cbc:
         *   local_c = base + drain_offset + 0x114c
         *   Push: base+0x70, &local_c, DTTE, line_data
         */
        local_c = (m68k_ptr_t)(uintptr_t)(base + drain_offset + SIO_DRAIN_HANDLER_OFFSET);

        dtte = &TERM_$DATA.dtte[TERM_$MAX_DTTE];

        SIO_$INIT_LINE(
            base + line_offset - SIO_LINE_DATA_ADJUST,          /* line data (TTY desc) */
            (void *)dtte,                                       /* DTTE entry */
            &local_c,                                           /* &(drain handler addr) */
            base + SIO_CONSOLE_HW_INFO_OFFSET                   /* console hardware info */
        );

        /*
         * Step 3: SIO_$INIT_DRAIN_HANDLER - Set up drain notification
         *
         * Assembly at 0xe32cc4-0xe32d02:
         *   local_c = base + line_offset - 0x384 (line data = context)
         *   local_8 = base + line_offset + 0x4e  (txbuf area = data)
         *   Push: &local_c, &local_8, DTTE, handler_record
         */
        local_c = (m68k_ptr_t)(uintptr_t)(base + line_offset - SIO_LINE_DATA_ADJUST);
        local_8 = (m68k_ptr_t)(uintptr_t)(base + line_offset + SIO_LINE_TXBUF_OFFSET);

        dtte = &TERM_$DATA.dtte[TERM_$MAX_DTTE];

        SIO_$INIT_DRAIN_HANDLER(
            (m68k_ptr_t *)(base + drain_offset + SIO_DRAIN_HANDLER_OFFSET),
            (void *)dtte,                                       /* DTTE (unused by callee) */
            &local_8,                                           /* &(txbuf area addr) */
            &local_c                                            /* &(line data addr) */
        );

        /*
         * Step 4: SIO_$INIT_DESC - Populate SIO descriptor
         *
         * Console uses KBD handlers (base+0x88) and console param block (base+0x58).
         * Owner and txbuf come from the console-specific per-port data area.
         *
         * Assembly at 0xe32d0a-0xe32d5c:
         *   local_c = base + console_offset + 0x1084 (owner = console term data)
         *   local_8 = base + console_offset + 0x1122 (txbuf = console txbuf)
         *   Push: vtable_ptr, context_ptr, base+0x88, &local_8, &local_c,
         *         DTTE, base+0x58, SIO_desc
         */
        local_c = (m68k_ptr_t)(uintptr_t)(base + console_offset + SIO_CONSOLE_TERM_OFFSET);
        local_8 = (m68k_ptr_t)(uintptr_t)(base + console_offset + SIO_CONSOLE_TXBUF_OFFSET);

        dtte = &TERM_$DATA.dtte[TERM_$MAX_DTTE];

        SIO_$INIT_DESC(
            (sio_desc_t *)(base + desc_offset + SIO_DESC_BASE_OFFSET),  /* SIO descriptor */
            base + SIO_CONSOLE_PARAM_OFFSET,                            /* console params */
            (void *)dtte,                                               /* DTTE entry */
            &local_c,                                                   /* &(owner addr) */
            &local_8,                                                   /* &(txbuf addr) */
            (m68k_ptr_t *)(base + SIO_CONSOLE_HANDLER_OFFSET),         /* KBD handlers */
            (m68k_ptr_t *)context_ptr,                                  /* context ptr */
            (char *)vtable_ptr                                          /* vtable */
        );

        /*
         * Step 5: SIO_$INIT_DTTE - Initialize DTTE with console discipline
         *
         * Assembly at 0xe32d64-0xe32d86:
         *   Push: DTTE, discipline=2
         */
        dtte = &TERM_$DATA.dtte[TERM_$MAX_DTTE];
        SIO_$INIT_DTTE(dtte, 2);   /* discipline 2 = console */

    } else {
        /*
         * ================================================================
         * Generic serial port initialization
         * ================================================================
         *
         * Generic ports use TTY handlers. The initialization sequence:
         *   1. SIO_$INIT_LINE - initialize TTY line descriptor
         *   2. SIO_$INIT_DESC - populate SIO descriptor
         *   3. SIO_$INIT_DTTE - initialize DTTE with discipline=0
         *   4. Either enable crash handler or call driver set_params
         */

        /* Precompute per-port offsets */
        int32_t desc_offset = (int32_t)port_num * SIO_DESC_STRIDE;
        int32_t line_offset = (int32_t)port_num * SIO_LINE_DATA_STRIDE;
        sio_desc_t *desc;

        /*
         * Step 1: SIO_$INIT_LINE
         *
         * For generic ports: line ID comes from SIO descriptor address.
         *
         * Assembly at 0xe32d8e-0xe32dda:
         *   local_c = base + desc_offset + 0xf78 (SIO desc address)
         *   Push: base+0x40, &local_c, DTTE, line_data
         */
        local_c = (m68k_ptr_t)(uintptr_t)(base + desc_offset + SIO_DESC_BASE_OFFSET);

        dtte = &TERM_$DATA.dtte[TERM_$MAX_DTTE];

        SIO_$INIT_LINE(
            base + line_offset - SIO_LINE_DATA_ADJUST,          /* line data (TTY desc) */
            (void *)dtte,                                       /* DTTE entry */
            &local_c,                                           /* &(SIO desc addr) */
            base + SIO_GENERIC_HW_INFO_OFFSET                   /* generic hardware info */
        );

        /*
         * Step 2: SIO_$INIT_DESC
         *
         * Generic ports use TTY handlers (base+0x18) and the base param block.
         * Owner = line data address; txbuf = line txbuf area.
         *
         * Assembly at 0xe32de2-0xe32e2e:
         *   local_8 = base + line_offset - 0x384 (owner = line data)
         *   local_c = base + line_offset + 0x4e  (txbuf area)
         *   Push: vtable_ptr, context_ptr, base+0x18, &local_c, &local_8,
         *         DTTE, base, SIO_desc
         */
        local_8 = (m68k_ptr_t)(uintptr_t)(base + line_offset - SIO_LINE_DATA_ADJUST);
        local_c = (m68k_ptr_t)(uintptr_t)(base + line_offset + SIO_LINE_TXBUF_OFFSET);

        dtte = &TERM_$DATA.dtte[TERM_$MAX_DTTE];

        SIO_$INIT_DESC(
            (sio_desc_t *)(base + desc_offset + SIO_DESC_BASE_OFFSET),  /* SIO descriptor */
            base + SIO_GENERIC_PARAM_OFFSET,                            /* generic params */
            (void *)dtte,                                               /* DTTE entry */
            &local_8,                                                   /* &(owner addr) */
            &local_c,                                                   /* &(txbuf addr) */
            (m68k_ptr_t *)(base + SIO_GENERIC_HANDLER_OFFSET),         /* TTY handlers */
            (m68k_ptr_t *)context_ptr,                                  /* context ptr */
            (char *)vtable_ptr                                          /* vtable */
        );

        /*
         * Step 3: SIO_$INIT_DTTE
         *
         * Assembly at 0xe32e36-0xe32e56:
         *   Push: DTTE, discipline=0
         */
        dtte = &TERM_$DATA.dtte[TERM_$MAX_DTTE];
        SIO_$INIT_DTTE(dtte, 0);   /* discipline 0 = serial TTY */

        /*
         * Step 4: Post-initialization
         *
         * Assembly at 0xe32e5c-0xe32e88:
         *   If flags < 0 (bit 7 set): enable crash handler
         *   If flags >= 0: call driver's set_params function
         */
        if (flags < 0) {
            /*
             * Enable crash handler on this serial port.
             * The ESC key (0x1B) triggers a system crash/debug break.
             *
             * Assembly at 0xe32e60-0xe32e70:
             *   st -(SP)                  ; push 0xFF (enable = true)
             *   move.w #0x1b00,-(SP)      ; push 0x1B (ESC key code)
             *   pea line_data             ; push TTY descriptor
             *   jsr TTY_$I_ENABLE_CRASH_FUNC
             *   clr.l (A4)               ; *status_ret = 0
             */
            TTY_$I_ENABLE_CRASH_FUNC(
                (tty_desc_t *)(base + line_offset - SIO_LINE_DATA_ADJUST),
                0x1B,       /* ESC key code */
                (char)-1    /* 0xFF = enable */
            );
            *status_ret = status_$ok;
        } else {
            /*
             * Call the driver's set_params function to apply initial
             * parameter configuration with all parameter bits set.
             *
             * Assembly at 0xe32e74-0xe32e88:
             *   A2 = base + port*0x78
             *   Push: status_ret, 0x3fff, &desc->params, desc->context
             *   A1 = desc->set_params
             *   jsr (A1)
             */
            desc = (sio_desc_t *)(base + desc_offset + SIO_DESC_BASE_OFFSET);
            ((void (*)(m68k_ptr_t, sio_params_t *, uint32_t, status_$t *))
                desc->set_params)(
                    desc->context,
                    &desc->params,
                    SIO_SET_PARAMS_ALL_MASK,
                    status_ret
            );
        }
    }

    /*
     * Common epilogue: return SIO descriptor pointer and increment DTTE count
     *
     * Assembly at 0xe32e8a-0xe32eac:
     *   A2 = base + port*0x78 + 0xf78 (recomputed)
     *   *desc_ret = A2
     *   TERM_$MAX_DTTE++
     */
    *desc_ret = (sio_desc_t *)(base + (int32_t)port_num * SIO_DESC_STRIDE
                                + SIO_DESC_BASE_OFFSET);
    TERM_$MAX_DTTE++;
}
