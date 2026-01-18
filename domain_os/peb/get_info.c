/*
 * peb/get_info.c - Get PEB Subsystem Information
 *
 * Returns information about the PEB subsystem state.
 *
 * Original address: 0x00E709E8 (84 bytes)
 */

#include "peb/peb_internal.h"

/*
 * PEB_$GET_INFO - Get PEB subsystem information
 *
 * Returns various flags indicating the state of the PEB subsystem:
 *
 * info_flags byte:
 *   Bit 7 (0x80): WCS microcode loaded
 *   Bit 6 (0x40): MC68881 save mode
 *   Bit 5 (0x20): Save pending flag
 *   Bit 4 (0x10): Unknown flag
 *   Bit 3 (0x08): Unknown flag
 *
 * info_byte: Additional configuration byte from PEB globals
 *
 * Assembly analysis:
 *   00e709e8    link.w A6,0x0
 *   00e709ec    movea.l (0x8,A6),A0       ; info_flags pointer
 *   00e709f0    clr.w (A0)                ; Clear both bytes
 *   00e709f2    tst.b (0x00e24c93).l      ; Test wcs_loaded
 *   00e709f8    bpl.b 0x00e709fe          ; If >= 0, skip
 *   00e709fa    bset.b #0x7,(A0)          ; Set bit 7
 *   00e709fe    tst.b (0x00e24c98).l      ; Test m68881_save_flag
 *   00e70a04    bpl.b 0x00e70a0a          ; If >= 0, skip
 *   00e70a06    bset.b #0x6,(A0)          ; Set bit 6
 *   00e70a0a    tst.b (0x00e24c94).l      ; Test savep_flag
 *   00e70a10    bpl.b 0x00e70a16          ; If >= 0, skip
 *   00e70a12    bset.b #0x5,(A0)          ; Set bit 5
 *   00e70a16    tst.b (0x00e24c95).l      ; Test flag_1d
 *   00e70a1c    bpl.b 0x00e70a22          ; If >= 0, skip
 *   00e70a1e    bset.b #0x3,(A0)          ; Set bit 3
 *   00e70a22    tst.b (0x00e24c99).l      ; Test flag_21
 *   00e70a28    bpl.b 0x00e70a2e          ; If >= 0, skip
 *   00e70a2a    bset.b #0x4,(A0)          ; Set bit 4
 *   00e70a2e    movea.l (0xc,A6),A1       ; info_byte pointer
 *   00e70a32    move.b (0x00e24c96).l,(A1) ; Copy info_byte
 *   00e70a38    unlk A6
 *   00e70a3a    rts
 *
 * Parameters:
 *   info_flags - Pointer to receive info flags byte
 *   info_byte  - Pointer to receive additional info byte
 */
void PEB_$GET_INFO(uint8_t *info_flags, uint8_t *info_byte)
{
    /* Clear the info flags (both bytes if treated as word) */
    *info_flags = 0;
    *(info_flags + 1) = 0;

    /* Set flag bits based on PEB state */

    /* Bit 7: WCS microcode loaded */
    if (PEB_$WCS_LOADED < 0) {
        *info_flags |= PEB_INFO_WCS_LOADED;
    }

    /* Bit 6: MC68881 save mode */
    if (PEB_$M68881_SAVE_FLAG < 0) {
        *info_flags |= PEB_INFO_M68881_MODE;
    }

    /* Bit 5: Save pending flag */
    if (PEB_$SAVEP_FLAG < 0) {
        *info_flags |= PEB_INFO_SAVEP_FLAG;
    }

    /* Bit 3: Unknown flag (flag_1d) */
    if (PEB_GLOBALS.flag_1d < 0) {
        *info_flags |= PEB_INFO_UNKNOWN_08;
    }

    /* Bit 4: Unknown flag (flag_21) */
    if (PEB_GLOBALS.flag_21 < 0) {
        *info_flags |= PEB_INFO_UNKNOWN_10;
    }

    /* Copy the info byte */
    *info_byte = PEB_$INFO_BYTE;
}
