/*
 * peb/assoc.c - PEB process association
 *
 * PEB_$ASSOC records the current process as the PEB owner and, the first
 * time, installs the three PEB register pages; PEB_$DISSOC removes the
 * per-process mirror page and clears the owner.
 *
 * Original addresses (SAU2 map: "I E5AD38 PEB_UNWIRED size = 200"):
 *   PEB_$ASSOC:  0x00E5AD38 (108 bytes)
 *   PEB_$DISSOC: 0x00E5ADA4 (38 bytes)
 *
 * Both routines address the PEB global block by its absolute address
 * (movea.l #0xe24c78,An); nothing is A5-relative here.
 */

#include "peb/peb_internal.h"

/*
 * PEB_$ASSOC - Associate PEB with current process
 *
 *   00e5ad38    link.w A6,-0x4
 *   00e5ad3c    movea.l #0xe24c78,A0              ; PEB globals
 *   00e5ad42    move.w (0x00e20608).l,(0x14,A0)   ; owner_pid  = PROC1_$CURRENT
 *   00e5ad4a    move.w (0x00e2060a).l,(0x16,A0)   ; owner_asid = PROC1_$AS_ID
 *   00e5ad52    tst.b (0x1f,A0)                   ; mmu_installed
 *   00e5ad56    bmi.b 0x00e5ada0                  ; already true -> done
 *   00e5ad58    st (0x1f,A0)                      ; mmu_installed = 0xFF
 *   00e5ad5c    pea (0x6).w                       ; flags 6
 *   00e5ad60    move.l #0xff7800,-(SP)            ; VA = PEB_$M_CS_PAGE (WCS)
 *   00e5ad66    pea (0x2e).w                      ; PPN 0x2E
 *   00e5ad6a    jsr 0x00e24048.l                  ; MMU_$INSTALL
 *   00e5ad70    lea (0xc,SP),SP
 *   00e5ad74    pea (0x6).w
 *   00e5ad78    move.l #0xff7000,-(SP)            ; VA = PEB_CTL page
 *   00e5ad7e    pea (0x2c).w                      ; PPN 0x2C
 *   00e5ad82    jsr 0x00e24048.l                  ; MMU_$INSTALL
 *   00e5ad88    lea (0xc,SP),SP
 *   00e5ad8c    pea (0x6).w
 *   00e5ad90    move.l #0xff7400,-(SP)            ; VA = PEB_$M_DCMD_PAGE (mirror)
 *   00e5ad96    pea (0x2d).w                      ; PPN 0x2D
 *   00e5ad9a    jsr 0x00e24048.l                  ; MMU_$INSTALL (args left for unlk)
 *   00e5ada0    unlk A6
 *   00e5ada2    rts
 */
void PEB_$ASSOC(void)
{
    /* 0x00E5AD42-0x00E5AD4A */
    PEB_$OWNER_PID = PROC1_$CURRENT;
    PEB_$OWNER_ASID = PROC1_$AS_ID;

    /* 0x00E5AD52-0x00E5AD56: Domain boolean, 0xFF tested with bmi */
    if ((int8_t)PEB_$MMU_INSTALLED >= 0) {
        /* 0x00E5AD58 */
        PEB_$MMU_INSTALLED = -1;   /* st: 0xFF */

        /* 0x00E5AD5C-0x00E5AD9A: three page installs, all with flags 6 */
        MMU_$INSTALL(0x2E, 0xFF7800, 0, 6);
        MMU_$INSTALL(0x2C, 0xFF7000, 0, 6);
        MMU_$INSTALL(0x2D, 0xFF7400, 0, 6);
    }
}

/*
 * PEB_$DISSOC - Disassociate PEB from current process
 *
 *   00e5ada4    link.w A6,-0x4
 *   00e5ada8    pea (A2)
 *   00e5adaa    movea.l #0xe24c78,A2              ; PEB globals
 *   00e5adb0    pea (0x2d).w                      ; PPN 0x2D
 *   00e5adb4    jsr 0x00e23d64.l                  ; MMU_$REMOVE
 *   00e5adba    clr.w (0x14,A2)                   ; owner_pid = 0
 *   00e5adbe    clr.b (0x1f,A2)                   ; mmu_installed = 0
 *   00e5adc2    movea.l (-0x8,A6),A2
 *   00e5adc6    unlk A6
 *   00e5adc8    rts
 */
void PEB_$DISSOC(void)
{
    /* 0x00E5ADB0-0x00E5ADB4 */
    MMU_$REMOVE(0x2D);

    /* 0x00E5ADBA-0x00E5ADBE */
    PEB_$OWNER_PID = 0;
    PEB_$MMU_INSTALLED = 0;
}
