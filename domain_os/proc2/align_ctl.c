/*
 * PROC2_$ALIGN_CTL - Alignment control
 *
 * Re-emitted from the image (0x00E41D74..0x00E41D9C, 42 bytes).  The
 * routine takes five arguments and ignores the first two; it simply
 * clears the three output cells.  The only reference is the SVC
 * dispatch table entry at 0x00E7BC12.
 *
 *   00e41d74  link.w A6,-0x10
 *   00e41d78  movem.l {A5 A2},-(SP)
 *   00e41d7c  lea (0xe7be84).l,A5        ; PROC2 module data base (unused)
 *   00e41d82  movea.l (0x10,A6),A0       ; arg 3
 *   00e41d86  clr.w (A0)                 ;   word cleared
 *   00e41d88  movea.l (0x14,A6),A1       ; arg 4
 *   00e41d8c  clr.l (A1)                 ;   longword cleared
 *   00e41d8e  movea.l (0x18,A6),A2       ; arg 5
 *   00e41d92  clr.l (A2)                 ;   longword cleared (status_$ok)
 *
 * Parameters:
 *   param_1    - (0x8,A6)  never read
 *   param_2    - (0xC,A6)  never read
 *   result_1   - (0x10,A6) output word, set to 0
 *   result_2   - (0x14,A6) output longword, set to 0
 *   status_ret - (0x18,A6) output status, set to status_$ok
 *
 * Original address: 0x00e41d74
 */

#include "proc2/proc2_internal.h"

void PROC2_$ALIGN_CTL(int param_1, int param_2, uint16_t *result_1,
                      uint32_t *result_2, status_$t *status_ret)
{
    (void)param_1;  /* (0x8,A6): never read */
    (void)param_2;  /* (0xC,A6): never read */

    *result_1 = 0;              /* 0x00E41D86 clr.w (A0) */
    *result_2 = 0;              /* 0x00E41D8C clr.l (A1) */
    *status_ret = status_$ok;   /* 0x00E41D92 clr.l (A2) */
}
