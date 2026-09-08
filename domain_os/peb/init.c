/*
 * peb/init.c - PEB Subsystem Initialization
 *
 * Initializes the Performance Enhancement Board (PEB) floating point
 * accelerator hardware and data structures at system boot.
 *
 * Original address: 0x00E31D0C (194 bytes)
 */

#include "peb/peb_internal.h"
#include "arch/arch.h"
#include "mmu/mmu.h"
#include "fim/fim.h"

/*
 * Global data definitions
 */

/* Per-process FP state storage - 58 processes * 28 bytes each = 1624 bytes */
peb_fp_state_t PEB_$WIRED_DATA_START[PEB_MAX_PROCESSES];

/* PEB_$STATUS_REG (0x00E24468) is the first cell of the PEB_ASM module and
 * is defined there (peb/sau2/int.s); peb/peb_data.c carries host storage. */

/*
 * ----------------------------------------------------------------------------
 * The two `pea (d,PC)` constant cells io_$probe is given
 * ----------------------------------------------------------------------------
 *
 * 0x00E31D7E `pea (0x4e,PC)`  -> 0x00E31D80 + 0x4E = 0x00E31DCE
 * 0x00E31D7A `pea (0x54,PC)`  -> 0x00E31D7C + 0x54 = 0x00E31DD0
 *
 * and the image bytes there (`gsk read 0xE31DCE 8`) are
 *
 *   00e31dce  00 01              word  0x0001
 *   00e31dd0  00 ff 70 00        long  0x00FF7000
 *
 * Both are passed by address, so they are named file-statics carrying those
 * values - not a raw literal pointer and not a `PTR_` placeholder object.
 * (source-fzke)
 */

/* 0x00E31DCE: the probe's device-type selector. */
static uint16_t peb_probe_type = 0x0001;

/* 0x00E31DD0: the address io_$probe pokes, the PEB control register at
 * 0xFF7000 that the MMU_$INSTALL immediately above has just mapped. */
static uint32_t peb_probe_addr = 0x00FF7000;

/* 0x00E31D76 `pea (-0x4,A6)`: io_$probe's four-byte scratch result. */
#define PEB_PROBE_RESULT_BYTES  4

/*
 * MMU_$INSTALL constants (0x00E31D5E-0x00E31D6C, 0x00E31DAC-0x00E31DBA).
 */
#define PEB_CTL_PPN         0x2C        /* pea (0x2c).w  */
#define PEB_CTL_VA          0xFF7000    /* move.l #0xff7000 */
#define PEB_WCS_PPN         0x2E        /* pea (0x2e).w  */
#define PEB_WCS_VA          0xFF7800    /* move.l #0xff7800 */
#define PEB_MMU_FLAGS       0x16        /* pea (0x16).w  */

/*
 * Exception vectors PEB_$INIT installs (both `move.l #handler,(vector).l`).
 */
#define PEB_FLINE_VECTOR    0x0000002C  /* 0x00E31D30: line-F emulator */
#define PEB_INT_VECTOR      0x00000070  /* 0x00E31D9C: PEB interrupt */

/* 0x00E31D3E `moveq #0x39,D0` / 0x00E31D48 `moveq #0x6,D1`: 58 slots of
 * seven longwords each (0x1C bytes, `lea (0x1c,A0),A0`). */
#define PEB_FP_STATE_LONGS  7

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
        *(void (**)(void))ARCH_VA_TO_PTR(PEB_FLINE_VECTOR) = FIM_$FLINE;
        return;
    }

    /* PEB mode - initialize per-process FP state storage */
    /* Zero all 58 process slots (28 bytes each = 7 longwords) */
    p = (uint32_t *)PEB_$WIRED_DATA_START;
    for (i = 0; i < PEB_MAX_PROCESSES; i++) {
        for (j = 0; j < PEB_FP_STATE_LONGS; j++) {
            *p++ = 0;
        }
    }

    /* Install MMU mapping for PEB control register at 0xFF7000 */
    /* PPN 0x2C maps to VA 0xFF7000 with flags 0x16 */
    MMU_$INSTALL(PEB_CTL_PPN, PEB_CTL_VA, PEB_MMU_FLAGS);

    /* Probe for PEB hardware */
    {
        uint8_t probe_result[PEB_PROBE_RESULT_BYTES];
        int8_t found;

        /*
         * 0x00E31D76-0x00E31D8E: three arguments, all by address - the type
         * word at 0x00E31DCE, the address longword at 0x00E31DD0 and the
         * frame scratch at A6-0x4.  The result is a Domain boolean tested
         * `tst.b D0b` / `bmi`.
         */
        found = io_$probe(&peb_probe_type, &peb_probe_addr, probe_result);

        if (found < 0) {
            /* PEB hardware found - install interrupt handler and WCS mapping */

            /* Install PEB interrupt handler at vector 0x70 */
            /* Vector 0x70 = interrupt level for PEB */
            *(void (**)(void))ARCH_VA_TO_PTR(PEB_INT_VECTOR) = PEB_$INT;

            /* Mark PEB as installed */
            PEB_$INSTALLED = 0xFF;

            /* Install MMU mapping for WCS at 0xFF7800 */
            /* PPN 0x2E maps to VA 0xFF7800 with flags 0x16 */
            MMU_$INSTALL(PEB_WCS_PPN, PEB_WCS_VA, PEB_MMU_FLAGS);

            /* Clear PEB control register to initialize hardware */
            PEB_CTL = 0;
        } else {
            /* PEB hardware not found - remove the control register mapping */
            MMU_$REMOVE(PEB_CTL_PPN);
        }
    }
}
