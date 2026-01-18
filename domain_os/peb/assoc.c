/*
 * peb/assoc.c - PEB Process Association Functions
 *
 * Associates and disassociates the PEB with processes during
 * context switching.
 *
 * Original addresses:
 *   PEB_$ASSOC: 0x00E5AD38 (108 bytes)
 *   PEB_$DISSOC: 0x00E5ADA4 (38 bytes)
 */

#include "peb/peb_internal.h"
#include "mmu/mmu.h"

/*
 * PEB_$ASSOC - Associate PEB with current process
 *
 * Sets up the PEB for the current process by recording the current
 * process/AS ID and installing MMU mappings for the PEB registers.
 * Called during process context switch when switching TO a process
 * that needs to use the PEB.
 *
 * Assembly analysis:
 *   00e5ad38    link.w A6,-0x4
 *   00e5ad3c    movea.l #0xe24c78,A0     ; PEB globals base
 *   00e5ad42    move.w (0x00e20608).l,(0x14,A0) ; owner_pid = PROC1_$CURRENT
 *   00e5ad4a    move.w (0x00e2060a).l,(0x16,A0) ; owner_asid = PROC1_$AS_ID
 *   00e5ad52    tst.b (0x1f,A0)          ; Test mmu_installed flag
 *   00e5ad56    bmi.b 0x00e5ada0         ; If already installed, skip
 *   00e5ad58    st (0x1f,A0)             ; Set mmu_installed = 0xFF
 *   00e5ad5c    pea (0x6).w              ; flags = 6
 *   00e5ad60    move.l #0xff7800,-(SP)   ; VA = 0xFF7800 (WCS)
 *   00e5ad66    pea (0x2e).w             ; PPN = 0x2E
 *   00e5ad6a    jsr 0x00e24048.l         ; MMU_$INSTALL
 *   00e5ad70    lea (0xc,SP),SP
 *   00e5ad74    pea (0x6).w              ; flags = 6
 *   00e5ad78    move.l #0xff7000,-(SP)   ; VA = 0xFF7000 (PEB_CTL)
 *   00e5ad7e    pea (0x2c).w             ; PPN = 0x2C
 *   00e5ad82    jsr 0x00e24048.l         ; MMU_$INSTALL
 *   00e5ad88    lea (0xc,SP),SP
 *   00e5ad8c    pea (0x6).w              ; flags = 6
 *   00e5ad90    move.l #0xff7400,-(SP)   ; VA = 0xFF7400 (private mirror)
 *   00e5ad96    pea (0x2d).w             ; PPN = 0x2D
 *   00e5ad9a    jsr 0x00e24048.l         ; MMU_$INSTALL
 *   00e5ada0    unlk A6
 *   00e5ada2    rts
 */
void PEB_$ASSOC(void)
{
    /* Record current process as PEB owner */
    PEB_$OWNER_PID = PROC1_$CURRENT;
    PEB_$OWNER_ASID = PROC1_$AS_ID;

    /* Install MMU mappings if not already installed */
    if (PEB_$MMU_INSTALLED >= 0) {
        /* Mark as installed */
        PEB_$MMU_INSTALLED = 0xFF;

        /* Install mapping for WCS at 0xFF7800 */
        MMU_$INSTALL(0x2E, 0xFF7800, 6);

        /* Install mapping for PEB control register at 0xFF7000 */
        MMU_$INSTALL(0x2C, 0xFF7000, 6);

        /* Install mapping for private register mirror at 0xFF7400 */
        MMU_$INSTALL(0x2D, 0xFF7400, 6);
    }
}

/*
 * PEB_$DISSOC - Disassociate PEB from current process
 *
 * Removes the PEB MMU mappings and clears the owner information
 * when switching away from a process that was using the PEB.
 *
 * Assembly analysis:
 *   00e5ada4    link.w A6,-0x4
 *   00e5ada8    pea (A2)
 *   00e5adaa    movea.l #0xe24c78,A2     ; PEB globals base
 *   00e5adb0    pea (0x2d).w             ; PPN = 0x2D (private mirror)
 *   00e5adb4    jsr 0x00e23d64.l         ; MMU_$REMOVE
 *   00e5adba    clr.w (0x14,A2)          ; owner_pid = 0
 *   00e5adbe    clr.b (0x1f,A2)          ; mmu_installed = 0
 *   00e5adc2    movea.l (-0x8,A6),A2
 *   00e5adc6    unlk A6
 *   00e5adc8    rts
 */
void PEB_$DISSOC(void)
{
    /* Remove the private register mirror mapping */
    MMU_$REMOVE(0x2D);

    /* Clear owner information */
    PEB_$OWNER_PID = 0;

    /* Mark MMU as not installed */
    PEB_$MMU_INSTALLED = 0;
}
