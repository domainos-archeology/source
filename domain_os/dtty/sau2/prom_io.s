| dtty/sau2/prom_io.s - PROM I/O routines for Display TTY
|
| These functions call PROM entry points for basic console I/O.
| They save and restore all caller-saved registers and protect
| against interrupt changes using SR save/restore.
|
| Original addresses:
|   DTTY_$PUTC:        0x00E2E018
|   DTTY_$CLEAR_SCREEN: 0x00E2E048

        .text
        .even

| PROM entry point addresses (low memory)
        .equ    PROM_PUTC_ADDR,   0x00000108
        .equ    PROM_CLEAR_ADDR,  0x00000140

|------------------------------------------------------------------------------
| DTTY_$PUTC - Output a single character through PROM
|
| Calls the PROM character output routine at the address stored
| in low memory location 0x108.
|
| Parameters:
|   4(SP) - Pointer to character to output
|
| Original address: 0x00E2E018
|------------------------------------------------------------------------------
        .globl  DTTY_$PUTC
DTTY_$PUTC:
        movem.l %d2-%d7/%a2-%a4,-(%sp)  | Save registers
        movea.l (0x28,%sp),%a0          | Get char pointer (offset past saved regs)
        move.b  (%a0),%d1               | Load character into D1
        movea.l (PROM_PUTC_ADDR).w,%a0  | Get PROM PUTC address from low memory
        move    %sr,-(%sp)              | Save status register
        jsr     (%a0)                   | Call PROM routine
        move    (%sp)+,%sr              | Restore status register
        movem.l (%sp)+,%d2-%d7/%a2-%a4  | Restore registers
        rts

|------------------------------------------------------------------------------
| DTTY_$CLEAR_SCREEN - Clear the display through PROM
|
| Calls the PROM screen clear routine at the address stored
| in low memory location 0x140. Uses function code 3 for full clear.
|
| Parameters:
|   None
|
| Original address: 0x00E2E048
|------------------------------------------------------------------------------
        .globl  DTTY_$CLEAR_SCREEN
DTTY_$CLEAR_SCREEN:
        movem.l %d2-%d7/%a2-%a4,-(%sp)  | Save registers
        moveq   #3,%d0                   | Function code 3 = full screen clear
        movea.l (PROM_CLEAR_ADDR).w,%a0 | Get PROM clear address from low memory
        move    %sr,-(%sp)              | Save status register
        jsr     (%a0)                   | Call PROM routine
        move    (%sp)+,%sr              | Restore status register
        movem.l (%sp)+,%d2-%d7/%a2-%a4  | Restore registers
        rts

        .end
