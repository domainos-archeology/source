#include "peb/peb_internal.h"

status_$t PEB_interrupt = status_$peb_interrupt;
/* 0x00E70950: 80 24 00 01 - shared by PEB_$TOUCH and peb_$cleanup_internal. */
status_$t PEB_FPU_Is_Hung_Err = status_$peb_fpu_is_hung | 0x80000000;

/*
 * PEB_$INFO - the PEB_PARITY data segment (map "D E24C78 PEB_PARITY size =
 * 24"), layout in peb/peb.h.  Zero in the image (`gsk read 0xE24C78 0x24`);
 * PEB_$INIT initialises the eventcount (0x00E31D12) and sets the flags.
 */
MODULE_DATA_DEFINE(peb_globals_t, PEB_$INFO, 0x00E24C78);

/* M68881_EXISTS (0xE8180C, PEB_UNWIRED): zero in the image. */
volatile int8_t M68881_EXISTS;

/*
 * The PEB_ASM module, 0x00E24468..0x00E244F0 (map: "D E24468 PEB_ASM
 * size = 88"), is hand-written assembly and is emitted as peb/sau2/int.s.
 * All three of its symbols live there on the m68k:
 *
 *   0x00E24468  PEB_$STATUS_REG      the latched 0x000070F4 exception status
 *   0x00E2446C  PEB_$INT             the interrupt handler
 *   0x00E24478  PEB_$DISP_INT_ADDR   the 32-bit operand of PEB_$INT's opening
 *                                    `jmp <abs>.l` (0x00E24476: 4E F9 00 E2
 *                                    1F 20), so writing it re-targets that
 *                                    jump.  The image value is 0x00E21F20 =
 *                                    FIM_$SPURIOUS_INT; SMD_$INTERRUPT_INIT
 *                                    overwrites it with SMD_$DISP1_INT when
 *                                    the PEB routes the display interrupt
 *                                    (smd/interrupt_init.c).
 *
 * No C file defines the two data cells: a host test that needs one defines
 * it (source-702z removed host-only definitions from this file).
 */
