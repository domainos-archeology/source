#include "peb/peb_internal.h"

status_$t PEB_interrupt = status_$peb_interrupt;
status_$t PEB_FPU_Is_Hung_Err = status_$peb_fpu_is_hung | 0x80000000;
status_$t PEB_WCS_Verify_Failed_Err = status_$peb_wcs_verify_failed;

/*
 * Constant pointer cells from the code segment (see peb_internal.h).
 * Original values read from the binary.
 */
void *PTR_PEB_CTL_00e31dd0 = (void *)0x00FF7000;               /* 0x00E31DD0 */
void *PTR_PEB_$WIRED_DATA_START_00e322dc = (void *)0x00E84E80; /* 0x00E322DC */
void *PTR_PEB_$WIRED_DATA_END_00e322e0 = (void *)0x00E854D8;   /* 0x00E322E0 */
void *PTR_PEB_$TOUCH_00e322e4 = (void *)0x00E70810;            /* 0x00E322E4 */
void *PTR_PEB_$WIRED_CODE_END_00e322e8 = (void *)0x00E70A3E;   /* 0x00E322E8 */

/*
 * Host-build storage for the two exported PEB feature flags declared in
 * peb/peb.h.  On ARCH_M68K those are absolute-address macros over the PEB
 * global block (0x00E24C92 / 0x00E24C98) and no object is needed.
 */
#if !defined(ARCH_M68K)
int8_t peb_$installed_flag = 0;
int8_t m68881_$save_flag = 0;
#endif

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
 * The host build has no .s, so the two data cells get plain storage here.
 */
#if !defined(ARCH_M68K)
uint32_t PEB_$STATUS_REG = 0;
void **PEB_$DISP_INT_ADDR = (void **)0x00E21F20;
#endif
