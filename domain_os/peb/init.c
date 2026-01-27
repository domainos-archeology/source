/*
 * peb/init.c - PEB Subsystem Initialization
 *
 * Initializes the Performance Enhancement Board (PEB) floating point
 * accelerator hardware and data structures at system boot.
 *
 * Original address: 0x00E31D0C (194 bytes)
 */

#include "peb/peb_internal.h"
#include "mmu/mmu.h"
#include "fim/fim_internal.h"

/*
 * Global data definitions
 */

/* Per-process FP state storage - 58 processes * 28 bytes each = 1624 bytes */
peb_fp_state_t PEB_$WIRED_DATA_START[PEB_MAX_PROCESSES];

/* PEB status register shadow */
uint32_t PEB_$STATUS_REG;

/*
 * External function for probing PEB hardware
 * Original address: 0x00E29138
 * Returns < 0 if hardware found, >= 0 if not found
 */
extern int8_t FUN_00e29138(const void *probe_data, const void **hw_addr, void *result);

/*
 * Probe data and hardware address pointer for PEB detection
 * Original addresses: 0x00E31DCE (probe data), 0x00E31DD0 (hw addr ptr)
 */
extern const void *PTR_PEB_CTL_00e31dd0;

/*
 * PEB_$INIT - Initialize PEB subsystem
 *
 * Assembly analysis:
 *   00e31d0c    link.w A6,-0xc
 *   00e31d10    move.l D2,-(SP)
 *   00e31d12    pea (0xe24c80).l         ; EC_$INIT(&eventcount)
 *   00e31d18    jsr 0x00e151fe.l
 *   00e31d1e    addq.w #0x4,SP
 *   00e31d20    movea.l #0xe8180c,A0     ; M68881_EXISTS
 *   00e31d26    tst.b (A0)
 *   00e31d28    bpl.b 0x00e31d3e         ; if M68881_EXISTS >= 0, skip 68881 setup
 *   00e31d2a    st (0x00e24c98).l        ; Set M68881_$SAVE_FLAG = 0xFF
 *   00e31d30    move.l #0xe21acc,(0x0000002c).l ; Install FIM_$FLINE at vector 0x2C
 *   00e31d3a    bra.w 0x00e31dc6         ; Done
 *   00e31d3e    moveq #0x39,D0           ; Loop 58 times (0x3A processes)
 *   00e31d40    movea.l #0xe84e80,A0     ; PEB_$WIRED_DATA_START
 *   ... (zeroing loop)
 *   00e31d5e    pea (0x16).w             ; flags = 0x16
 *   00e31d62    move.l #0xff7000,-(SP)   ; VA = 0xFF7000
 *   00e31d68    pea (0x2c).w             ; PPN = 0x2C
 *   00e31d6c    jsr 0x00e24048.l         ; MMU_$INSTALL
 *   ... (probe for hardware)
 *   00e31d9c    move.l #0xe2446c,(0x00000070).l ; Install PEB_$INT at vector 0x70
 *   00e31da6    st (0x00e24c92).l        ; Set PEB_$INSTALLED = 0xFF
 *   00e31dac    pea (0x16).w             ; flags
 *   00e31db0    move.l #0xff7800,-(SP)   ; VA = 0xFF7800 (WCS)
 *   00e31db6    pea (0x2e).w             ; PPN = 0x2E
 *   00e31dba    jsr 0x00e24048.l         ; MMU_$INSTALL
 *   00e31dc0    clr.w (0x00ff7000).l     ; Clear PEB_CTL
 */
void PEB_$INIT(void)
{
    int i, j;
    uint32_t *p;

    /* Initialize the PEB event counter */
    EC_$INIT(&PEB_$EVENTCOUNT);

    /* Check if MC68881 is present instead of PEB */
    if (M68881_EXISTS < 0) {
        /* MC68881 mode - set save flag and install F-line handler */
        PEB_$M68881_SAVE_FLAG = 0xFF;

        /* Install FIM F-line handler at vector 0x2C (F-line exception) */
        /* Vector 0x2C = interrupt vector for F-line (0xB * 4 = 0x2C) */
        *(void (**)(void))0x0000002C = FIM_$FLINE;
        return;
    }

    /* PEB mode - initialize per-process FP state storage */
    /* Zero all 58 process slots (28 bytes each = 7 longwords) */
    p = (uint32_t *)PEB_$WIRED_DATA_START;
    for (i = 0; i < PEB_MAX_PROCESSES; i++) {
        for (j = 0; j < 7; j++) {
            *p++ = 0;
        }
    }

    /* Install MMU mapping for PEB control register at 0xFF7000 */
    /* PPN 0x2C maps to VA 0xFF7000 with flags 0x16 */
    MMU_$INSTALL(0x2C, 0xFF7000, 0x16);

    /* Probe for PEB hardware */
    {
        uint8_t probe_result[4];
        int8_t found;

        /* Note: The probe function checks if hardware responds at the given address */
        /* Parameters appear to be: probe data, hw address pointer, result buffer */
        found = FUN_00e29138((const void *)0xE31DCE, &PTR_PEB_CTL_00e31dd0, probe_result);

        if (found < 0) {
            /* PEB hardware found - install interrupt handler and WCS mapping */

            /* Install PEB interrupt handler at vector 0x70 */
            /* Vector 0x70 = interrupt level for PEB */
            *(void (**)(void))0x00000070 = PEB_$INT;

            /* Mark PEB as installed */
            PEB_$INSTALLED = 0xFF;

            /* Install MMU mapping for WCS at 0xFF7800 */
            /* PPN 0x2E maps to VA 0xFF7800 with flags 0x16 */
            MMU_$INSTALL(0x2E, 0xFF7800, 0x16);

            /* Clear PEB control register to initialize hardware */
            PEB_CTL = 0;
        } else {
            /* PEB hardware not found - remove the control register mapping */
            MMU_$REMOVE(0x2C);
        }
    }
}
