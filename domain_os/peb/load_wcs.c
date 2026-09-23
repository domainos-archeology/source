/*
 * peb/load_wcs.c - PEB WCS (writable control store) loading
 *
 * PEB_$LOAD_WCS is called once from the boot path (0x00E3473E).  It has two
 * disjoint arms selected by the flags in the PEB global block:
 *
 *   - MC68881 present or SAVEP pending (0xE24C98 / 0xE24C94 negative):
 *     create an unnamed file, lock it, and MST_$MAPS 0x497A bytes of it as
 *     the FP save area, recording the address in FP_$SAVEP.
 *   - otherwise, if the PEB board is installed (0xE24C92 negative): resolve
 *     /sys/peb2_microcode, lock and map it, write every 8-byte entry into
 *     the WCS, read each one back (CRASH_SYSTEM on a mismatch), unmap and
 *     unlock, wire the PEB code and data areas, and enable the board.
 *
 * Every step is followed by a call to the nested function CHECK_ERR, which
 * reads the enclosing frame's status through the static link, prints a
 * warning and returns true if the step failed.
 *
 * Original addresses (SAU2 map: "I E31D0C PEB_UNWIRED size = 5F8", which
 * ends at 0x00E32304 and also holds every constant cell below):
 *   peb_$write_wcs:          0x00E31DD4 (122 bytes)
 *   peb_$read_wcs:           0x00E31E4E (122 bytes)
 *   PEB_$LOAD_WCS_CHECK_ERR: 0x00E31EC8 (144 bytes)
 *   PEB_$LOAD_WCS:           0x00E31FD8 (684 bytes)
 *
 * Only PEB_$LOAD_WCS has a map symbol; the other three are module-local
 * (CHECK_ERR is a Pascal function nested inside PEB_$LOAD_WCS) and are
 * static here.
 */

#include "peb/peb_internal.h"
#include "file/file.h"
#include "mst/mst.h"
#include "name/name.h"
#include "vfmt/vfmt.h"

/*
 * ============================================================================
 * Constant cells and strings in the PEB_UNWIRED code segment
 * ============================================================================
 * Each is reached only by `pea (d,PC)` from the routines in this file; the
 * cell address is the instruction address + 2 + d.  Image bytes from
 * `gsk read`.  VFMT strings end in "%$" (or "%." for the ones that add a
 * newline) as in the image.
 */

/* 0x00E31F58: 00 00 00 00 - the zero longword handed to MST_$MAP twice
 * (start and extend) and, twice per call, to every VFMT_$WRITE10 below. */
static const uint32_t peb_$zero_long = 0;

/* 0x00E31F5C */
static const char peb_$fmt_peb_disabled[] = " \"%a\" -- %lh%/PEB is disabled.%.";

/* 0x00E31F7C: 19 characters followed by a NUL at 0x00E31F8F. */
static char peb_$microcode_path[] = "/sys/peb2_microcode";

/* 0x00E31F90: 00 00 00 13 - the path length as a longword (VFMT %a). */
static const uint32_t peb_$path_len_long = 0x13;

/* 0x00E31F94 */
static const char peb_$fmt_68881_disabled[] = " 68881 savearea -- 68881 is disabled.%.";

/* 0x00E31FBC */
static const char peb_$fmt_warning[] = "%/%/%/Warning: Unable to %$";

/* 0x00E32288 */
static const char peb_$msg_create_file[] = "create file for%$";

/* 0x00E3229A: 00 0a - MST_$WIRE_AREA's page-list capacity. */
static const uint16_t peb_$wire_max_pages = 10;

/* 0x00E3229C */
static const char peb_$msg_map[] = "map%$";

/* 0x00E322A2: 00 06 - MST_$MAP mode word. */
static const uint16_t peb_$map_mode = 6;

/* 0x00E322A4 */
static const char peb_$msg_lock[] = "lock%$";

/* 0x00E322AA */
static const char peb_$msg_resolve[] = "resolve%$";

/* 0x00E322B4: 00 13 - the path length word for NAME_$RESOLVE. */
static int16_t peb_$path_len = 0x13;

/* 0x00E322B6 */
static const char peb_$msg_map_file[] = "map file for%$";

/* 0x00E322C4: 00 00 - FILE_$LOCK index word. */
static const uint16_t peb_$lock_index = 0;

/* 0x00E322C6: 00 04 - FILE_$LOCK mode word on the save-area arm. */
static const uint16_t peb_$lock_mode_4 = 4;

/* 0x00E322C8: 00 00 - the byte FILE_$LOCK reads as rights and MST_$MAP as
 * its concurrency selector.  The following byte, 0x00E322C9, is also 00. */
static const uint8_t peb_$lock_rights = 0;

/* 0x00E322CC */
static const char peb_$msg_lock_file[] = "lock file for%$";

/*
 * 0x00E322DC..0x00E322EB: the four longwords MST_$WIRE_AREA takes by
 * address (bead source-f3dk).  Image bytes:
 *   00 e8 4e 80  00 e8 54 d8  00 e7 08 10  00 e7 0a 3e
 * Their contents are the map symbols PEB_$WIRED_DATA_START (also
 * PEB_$REGS), PEB_$WIRED_DATA_END (= AUDIT_$DATA_START), PEB_$WIRED_PROC_START
 * (= PEB_$TOUCH) and PEB_$WIRED_PROC_END.
 */
static const uint32_t peb_$wired_data_start = 0x00E84E80u; /* 0x00E322DC */
static const uint32_t peb_$wired_data_end   = 0x00E854D8u; /* 0x00E322E0 */
static const uint32_t peb_$wired_proc_start = 0x00E70810u; /* 0x00E322E4 */
static const uint32_t peb_$wired_proc_end   = 0x00E70A3Eu; /* 0x00E322E8 */

/* 0x00E322EC */
static const char peb_$msg_unlock[] = "unlock%$";

/* 0x00E322F4 */
static const char peb_$msg_unmap[] = "unmap%$";

/* 0x00E322FC: 00 24 00 09 - the status CRASH_SYSTEM gets on a verify
 * mismatch; only this routine reaches it (0x00E321A4 `pea (0x156,PC)`). */
static status_$t PEB_WCS_Verify_Failed_Err = status_$peb_wcs_verify_failed;

/* 0x00E32300: 00 01 00 00 - MST_$MAP length longword (64 KB). */
static const uint32_t peb_$map_length = 0x00010000u;

/*
 * The FILE_$LOCK / FILE_$UNLOCK mode word on the microcode arm is the word
 * at 0x00E31DCE (00 01), which PEB_$INIT also passes to io_$probe: the one
 * object peb_$const_word_1 (defined in peb/init.c).
 */

/*
 * ============================================================================
 * peb_$write_wcs (0x00E31DD4) / peb_$read_wcs (0x00E31E4E)
 * ============================================================================
 * Both take a 16-bit WCS address at (0x8,A6) and an entry pointer at
 * (0xA,A6).  The caller allocates a 2-byte slot above the arguments
 * (`subq.l #2,SP`, cleaned up with the 8-byte pop) that neither routine
 * touches.
 *
 * The address's bits 7.. select the WCS page: they go into bits 4..9 of the
 * control shadow at 0xE24C90 (globals +0x18), which is written back and to
 * PEB_CTL.  The low 7 bits select one of 128 8-byte entries in the WCS page
 * at 0xFF7800 (= 0xFF7000 + 0x800, the way the code forms it).
 *
 *   00e31dd4    link.w A6,-0x8
 *   00e31dd8    move.l D2,-(SP)
 *   00e31dda    move.w (0x8,A6),D0w            ; addr
 *   00e31dde    movea.l (0xa,A6),A0            ; entry
 *   00e31de2    move.w (0x00e24c90).l,(-0x6,A6) ; ctl = shadow
 *   00e31dea    move.w D0w,D1w
 *   00e31dec    ext.l D1
 *   00e31dee    lsr.l #0x7,D1                  ; addr >> 7
 *   00e31df0    ext.w D1w
 *   00e31df2    andi.w #-0x3f1,(-0x6,A6)       ; ctl &= 0xFC0F
 *   00e31df8    lsl.w #0x4,D1w
 *   00e31dfa    andi.w #0x3f0,D1w              ; page bits
 *   00e31dfe    or.w D1w,(-0x6,A6)
 *   00e31e02    move.w (-0x6,A6),D1w
 *   00e31e06    move.w D1w,(0x00e24c90).l      ; shadow = ctl
 *   00e31e0c    move.w D1w,(0x00ff7000).l      ; PEB_CTL = ctl
 *   00e31e12    andi.w #0x7f,D0w
 *   00e31e16    movea.l #0xff7000,A1
 *   00e31e1c    lsl.w #0x2,D0w                 ; (addr & 0x7F) * 4
 *   00e31e1e    move.w D0w,D2w
 *   00e31e20    ext.l D2
 *   00e31e22    add.l D2,D2                    ; * 8
 *   00e31e24    lea (0x0,A1,D2*0x1),A1
 *   00e31e28    move.w (A0),(0x800,A1)         ; word0
 *   00e31e2c    move.w D0w,D2w
 *   00e31e2e    movea.l #0xff7000,A1
 *   00e31e34    add.w D2w,D2w
 *   00e31e36    lea (0x0,A1,D2w*0x1),A1
 *   00e31e3a    move.w (0x2,A0),(0x802,A1)     ; word1
 *   00e31e40    move.l (0x4,A0),(0x804,A1)     ; word2
 *   00e31e46    move.l (-0xc,A6),D2
 *   00e31e4a    unlk A6
 *   00e31e4c    rts
 *
 * peb_$read_wcs is byte-for-byte the same shape with the three moves
 * reversed (0x00E31EA2, 0x00E31EB4, 0x00E31EBA read the WCS into the entry).
 */

/* Select the WCS page for `addr` and return the entry's byte address. */
static volatile uint8_t *peb_$select_wcs(uint16_t addr)
{
    uint16_t ctl;
    uint16_t page_bits;
    uint16_t offset;

    /* 0x00E31DE2-0x00E31E0C */
    ctl = PEB_$CTL_SHADOW;
    page_bits = (uint16_t)(((uint32_t)(int32_t)(int16_t)addr >> 7) << 4) & PEB_CTL_WCS_PAGE_MASK;
    ctl = (uint16_t)(ctl & 0xFC0F);
    ctl |= page_bits;
    PEB_$CTL_SHADOW = ctl;
    PEB_CTL = ctl;

    /* 0x00E31E12-0x00E31E24: (addr & 0x7F) * 8 */
    offset = (uint16_t)((addr & 0x7F) << 2);
    return (volatile uint8_t *)PEB_WCS_BASE + (uint32_t)offset * 2u;
}

static void peb_$write_wcs(uint16_t addr, const peb_wcs_entry_t *data)
{
    volatile uint8_t *wcs = peb_$select_wcs(addr);

    /* 0x00E31E28-0x00E31E40 */
    *(volatile uint16_t *)(wcs + 0) = data->word0;
    *(volatile uint16_t *)(wcs + 2) = data->word1;
    *(volatile uint32_t *)(wcs + 4) = data->word2;
}

static void peb_$read_wcs(uint16_t addr, peb_wcs_entry_t *data)
{
    volatile uint8_t *wcs = peb_$select_wcs(addr);

    /* 0x00E31EA2-0x00E31EBA */
    data->word0 = *(volatile uint16_t *)(wcs + 0);
    data->word1 = *(volatile uint16_t *)(wcs + 2);
    data->word2 = *(volatile uint32_t *)(wcs + 4);
}

/*
 * ============================================================================
 * PEB_$LOAD_WCS_CHECK_ERR (0x00E31EC8) - nested function of PEB_$LOAD_WCS
 * ============================================================================
 * Takes one argument, the step name at (0x8,A6), and reads the enclosing
 * frame's `status` (-0x50,A6 of PEB_$LOAD_WCS) through the static link
 * (`movea.l (A6),A2`), so it carries an explicit uplevel parameter here.
 * Returns a Domain boolean in D0: true when the step failed.
 *
 *   00e31ec8    link.w A6,-0x8
 *   00e31ecc    pea (A2)
 *   00e31ece    movea.l (A6),A2                ; enclosing frame
 *   00e31ed0    tst.w (-0x4e,A2)               ; LOW word of status
 *   00e31ed4    beq.b 0x00e31f4e               ; zero -> false
 *   00e31ed6    pea (0x80,PC)                  ; &0x00E31F58 (zero)
 *   00e31eda    move.l (SP),-(SP)              ; again
 *   00e31edc    pea (0xde,PC)                  ; 0x00E31FBC "%/%/%/Warning: Unable to %$"
 *   00e31ee0    jsr 0x00e825f4.l               ; VFMT_$WRITE10
 *   00e31ee6    lea (0xc,SP),SP
 *   00e31eea    pea (0x6c,PC)                  ; &zero
 *   00e31eee    move.l (SP),-(SP)
 *   00e31ef0    move.l (0x8,A6),-(SP)          ; the step name
 *   00e31ef4    jsr 0x00e825f4.l               ; VFMT_$WRITE10
 *   00e31efa    lea (0xc,SP),SP
 *   00e31efe    tst.b (0x00e24c98).l           ; m68881_save_flag
 *   00e31f04    bpl.b 0x00e31f2e
 *   00e31f06    clr.b (0x00e24c98).l           ; m68881_save_flag = 0
 *   00e31f0c    pea (0x4a,PC)                  ; &zero
 *   00e31f10    move.l (SP),-(SP)
 *   00e31f12    pea (0x80,PC)                  ; 0x00E31F94 " 68881 savearea -- ..."
 *   00e31f16    jsr 0x00e825f4.l               ; VFMT_$WRITE10 (no pop; unlk)
 *   00e31f1c    clr.l (0x00e218d0).l           ; FP_$SAVEP = 0
 *   00e31f22    move.l #0xe2146c,(0x0000002c).l ; vector 11 (line-F) = FIM_$UII
 *   00e31f2c    bra.b 0x00e31f4a
 *   00e31f2e    pea (0x28,PC)                  ; &zero
 *   00e31f32    move.l (SP),-(SP)
 *   00e31f34    pea (-0x50,A2)                 ; &status (uplevel)
 *   00e31f38    pea (0x56,PC)                  ; &0x00E31F90 (long 0x13)
 *   00e31f3c    pea (0x3e,PC)                  ; 0x00E31F7C "/sys/peb2_microcode"
 *   00e31f40    pea (0x1a,PC)                  ; 0x00E31F5C " \"%a\" -- %lh%/PEB is disabled.%."
 *   00e31f44    jsr 0x00e825f4.l               ; VFMT_$WRITE10 (no pop; unlk)
 *   00e31f4a    st D0b                         ; true
 *   00e31f4c    bra.b 0x00e31f50
 *   00e31f4e    clr.b D0b                      ; false
 *   00e31f50    movea.l (-0xc,A6),A2
 *   00e31f54    unlk A6
 *   00e31f56    rts
 */
static boolean PEB_$LOAD_WCS_CHECK_ERR(const char *msg, status_$t *status)
{
    /* 0x00E31ED0-0x00E31ED4: only the low 16 bits of the status are tested */
    if ((*status & 0xFFFF) == 0) {
        return false;
    }

    /* 0x00E31ED6-0x00E31EFA */
    VFMT_$WRITE10(peb_$fmt_warning, &peb_$zero_long, &peb_$zero_long);
    VFMT_$WRITE10(msg, &peb_$zero_long, &peb_$zero_long);

    /* 0x00E31EFE-0x00E31F04 */
    if ((int8_t)PEB_$M68881_SAVE_FLAG < 0) {
        /* 0x00E31F06-0x00E31F22 */
        PEB_$M68881_SAVE_FLAG = 0;
        VFMT_$WRITE10(peb_$fmt_68881_disabled, &peb_$zero_long, &peb_$zero_long);
        FP_$SAVEP = 0;
        ARCH_VECTOR(11) = (void *)FIM_$UII;
    } else {
        /* 0x00E31F2E-0x00E31F44 */
        VFMT_$WRITE10(peb_$fmt_peb_disabled, peb_$microcode_path,
                      &peb_$path_len_long, status,
                      &peb_$zero_long, &peb_$zero_long);
    }

    /* 0x00E31F4A */
    return true;
}

/*
 * ============================================================================
 * PEB_$LOAD_WCS (0x00E31FD8)
 * ============================================================================
 * Frame (-0x64):
 *   -0x28  wired_pages[10]   (0x28 bytes) MST_$WIRE_AREA page list
 *   -0x30  verify            (8 bytes)    peb_$read_wcs result
 *   -0x38  file_uid          (8 bytes)
 *   -0x40  maps_length       (4 bytes)    holds 0x497A for MST_$MAPS
 *   -0x44  map_va            (4 bytes)    copy of the mapped address for MST_$UNMAP
 *   -0x48  lock_info         (4 bytes)    FILE_$LOCK's 5th argument
 *   -0x4C  map_info          (4 bytes)    MST_$MAPS/MST_$MAP/MST_$UNMAP map_info
 *   -0x50  status            (4 bytes)    read uplevel by CHECK_ERR
 *   -0x52  ctl               (word)
 *   -0x54  wire_count1       (word)
 *   -0x56  wire_count2       (word)
 *   -0x58  ctl_page_byte     (byte)       the 0xFF73FC read
 *   -0x62  wire_remaining    (word)
 *
 *   00e31fd8    link.w A6,-0x64
 *   00e31fdc    movem.l {A3 A2 D3 D2},-(SP)
 *   00e31fe0    tst.b (0x00e24c98).l          ; m68881_save_flag
 *   00e31fe6    bmi.b 0x00e31ff2
 *   00e31fe8    tst.b (0x00e24c94).l          ; savep_flag
 *   00e31fee    bpl.w 0x00e320a8              ; neither -> PEB arm
 *   --- save-area arm ---
 *   00e31ff2    pea (-0x50,A6)                ; &status
 *   00e31ff6    pea (-0x38,A6)                ; &file_uid
 *   00e31ffa    move.l #0xe1737c,-(SP)        ; &UID_$NIL
 *   00e32000    jsr 0x00e5d778.l              ; FILE_$CREATE
 *   00e32006    lea (0xc,SP),SP
 *   00e3200a    pea (0x27c,PC)                ; 0x00E32288 "create file for%$"
 *   00e3200e    bsr.w 0x00e31ec8              ; CHECK_ERR
 *   00e32012    addq.w #0x4,SP
 *   00e32014    tst.b D0b
 *   00e32016    bmi.w 0x00e3227e
 *   00e3201a    pea (-0x50,A6)                ; &status
 *   00e3201e    pea (-0x48,A6)                ; lock_info
 *   00e32022    pea (0x2a4,PC)                ; &0x00E322C8 rights
 *   00e32026    pea (0x29e,PC)                ; &0x00E322C6 mode 4
 *   00e3202a    pea (0x298,PC)                ; &0x00E322C4 index 0
 *   00e3202e    pea (-0x38,A6)                ; &file_uid
 *   00e32032    jsr 0x00e5eb20.l              ; FILE_$LOCK
 *   00e32038    lea (0x18,SP),SP
 *   00e3203c    pea (0x28e,PC)                ; 0x00E322CC "lock file for%$"
 *   00e32040    bsr.w 0x00e31ec8
 *   00e32044    addq.w #0x4,SP
 *   00e32046    tst.b D0b
 *   00e32048    bmi.w 0x00e3227e
 *   00e3204c    move.l #0x497a,(-0x40,A6)
 *   00e32054    pea (-0x50,A6)                ; status
 *   00e32058    pea (-0x4c,A6)                ; map_info
 *   00e3205c    st -(SP)                      ; access_rights = true
 *   00e3205e    clr.l -(SP)                   ; area_size = 0
 *   00e32060    move.w #0x16,-(SP)            ; area_id = 0x16
 *   00e32064    move.l (-0x40,A6),-(SP)       ; length = 0x497A
 *   00e32068    clr.l -(SP)                   ; start_va = 0
 *   00e3206a    pea (-0x38,A6)                ; &file_uid
 *   00e3206e    st -(SP)                      ; direction = true
 *   00e32070    clr.w -(SP)                   ; asid = 0
 *   00e32072    jsr 0x00e43982.l              ; MST_$MAPS
 *   00e32078    lea (0x20,SP),SP
 *   00e3207c    move.l A0,(0x00e218d0).l      ; FP_$SAVEP = result
 *   00e32082    pea (0x232,PC)                ; 0x00E322B6 "map file for%$"
 *   00e32086    bsr.w 0x00e31ec8
 *   00e3208a    addq.w #0x4,SP
 *   00e3208c    tst.b D0b
 *   00e3208e    bmi.w 0x00e3227e
 *   00e32092    movea.l (0x00e218d0).l,A0
 *   00e32098    moveq #0x12,D0                ; 19 iterations
 *   00e3209a    movea.l A0,A0
 *   00e3209c    lea (0x400,A0),A0             ; A0 += 0x400 (nothing read)
 *   00e320a0    dbf D0w,0x00e3209c
 *   00e320a4    bra.w 0x00e3227e
 *   --- PEB arm ---
 *   00e320a8    tst.b (0x00e24c92).l          ; installed
 *   00e320ae    bpl.w 0x00e3227e
 *   00e320b2    pea (-0x50,A6)                ; &status
 *   00e320b6    pea (-0x38,A6)                ; &file_uid
 *   00e320ba    pea (0x1f8,PC)                ; &0x00E322B4 (word 19)
 *   00e320be    pea (-0x144,PC)               ; 0x00E31F7C "/sys/peb2_microcode"
 *   00e320c2    jsr 0x00e4a258.l              ; NAME_$RESOLVE
 *   00e320c8    lea (0x10,SP),SP
 *   00e320cc    pea (0x1dc,PC)                ; 0x00E322AA "resolve%$"
 *   00e320d0    bsr.w 0x00e31ec8
 *   00e320d4    addq.w #0x4,SP
 *   00e320d6    tst.b D0b
 *   00e320d8    bmi.w 0x00e3227e
 *   00e320dc    pea (-0x50,A6)
 *   00e320e0    pea (-0x48,A6)                ; lock_info
 *   00e320e4    pea (0x1e2,PC)                ; &0x00E322C8 rights
 *   00e320e8    pea (-0x31c,PC)               ; &0x00E31DCE mode 1
 *   00e320ec    pea (0x1d6,PC)                ; &0x00E322C4 index 0
 *   00e320f0    pea (-0x38,A6)                ; &file_uid
 *   00e320f4    jsr 0x00e5eb20.l              ; FILE_$LOCK
 *   00e320fa    lea (0x18,SP),SP
 *   00e320fe    pea (0x1a4,PC)                ; 0x00E322A4 "lock%$"
 *   00e32102    bsr.w 0x00e31ec8
 *   00e32106    addq.w #0x4,SP
 *   00e32108    tst.b D0b
 *   00e3210a    bmi.w 0x00e3227e
 *   00e3210e    pea (-0x50,A6)                ; status
 *   00e32112    pea (-0x4c,A6)                ; map_info
 *   00e32116    pea (0x1b0,PC)                ; &0x00E322C8 concur
 *   00e3211a    pea (-0x1c4,PC)               ; &0x00E31F58 extend 0
 *   00e3211e    pea (0x182,PC)                ; &0x00E322A2 mode 6
 *   00e32122    pea (0x1dc,PC)                ; &0x00E32300 length 0x10000
 *   00e32126    pea (-0x1d0,PC)               ; &0x00E31F58 start 0
 *   00e3212a    pea (-0x38,A6)                ; &file_uid
 *   00e3212e    jsr 0x00e4386c.l              ; MST_$MAP
 *   00e32134    lea (0x20,SP),SP
 *   00e32138    movea.l A0,A2                 ; A2 = mapped header
 *   00e3213a    pea (0x160,PC)                ; 0x00E3229C "map%$"
 *   00e3213e    bsr.w 0x00e31ec8
 *   00e32142    addq.w #0x4,SP
 *   00e32144    tst.b D0b
 *   00e32146    bmi.w 0x00e3227e
 *   00e3214a    move.w (0x2,A2),D0w           ; entry_count
 *   00e3214e    subq.w #0x1,D0w
 *   00e32150    bmi.b 0x00e32172              ; count == 0 -> skip
 *   00e32152    move.w D0w,D2w                ; dbf counter
 *   00e32154    clr.w D3w                     ; i = 0
 *   00e32156    lea (A2),A3
 *   00e32158    subq.l #0x2,SP                ; unused 2-byte slot
 *   00e3215a    pea (0x4,A3)                  ; &entries[i]
 *   00e3215e    move.w D3w,D0w
 *   00e32160    add.w (A2),D0w                ; start_addr + i
 *   00e32162    move.w D0w,-(SP)
 *   00e32164    bsr.w 0x00e31dd4              ; peb_$write_wcs
 *   00e32168    addq.w #0x8,SP
 *   00e3216a    addq.w #0x1,D3w
 *   00e3216c    addq.l #0x8,A3
 *   00e3216e    dbf D2w,0x00e32158
 *   00e32172    move.w (0x2,A2),D0w           ; second pass: verify
 *   00e32176    subq.w #0x1,D0w
 *   00e32178    bmi.b 0x00e321b8
 *   00e3217a    move.w D0w,D2w
 *   00e3217c    clr.w D3w
 *   00e3217e    lea (A2),A3
 *   00e32180    subq.l #0x2,SP
 *   00e32182    pea (-0x30,A6)                ; &verify
 *   00e32186    move.w D3w,D0w
 *   00e32188    add.w (A2),D0w
 *   00e3218a    move.w D0w,-(SP)
 *   00e3218c    bsr.w 0x00e31e4e              ; peb_$read_wcs
 *   00e32190    addq.w #0x8,SP
 *   00e32192    lea (-0x30,A6),A0
 *   00e32196    lea (0x4,A3),A1
 *   00e3219a    moveq #0x1,D0
 *   00e3219c    cmpm.l (A1)+,(A0)+            ; word0:word1
 *   00e3219e    bne.b 0x00e321a4
 *   00e321a0    cmpm.l (A1)+,(A0)+            ; word2
 *   00e321a2    beq.b 0x00e321b0
 *   00e321a4    pea (0x156,PC)                ; &0x00E322FC verify-failed status
 *   00e321a8    jsr 0x00e1e700.l              ; CRASH_SYSTEM
 *   00e321ae    addq.w #0x4,SP
 *   00e321b0    addq.w #0x1,D3w
 *   00e321b2    addq.l #0x8,A3
 *   00e321b4    dbf D2w,0x00e32180
 *   00e321b8    move.l A2,(-0x44,A6)          ; map_va = header address
 *   00e321bc    pea (-0x50,A6)                ; status
 *   00e321c0    pea (-0x4c,A6)                ; map_info
 *   00e321c4    pea (-0x44,A6)                ; &map_va
 *   00e321c8    pea (-0x38,A6)                ; &file_uid
 *   00e321cc    jsr 0x00e4472e.l              ; MST_$UNMAP
 *   00e321d2    lea (0x10,SP),SP
 *   00e321d6    pea (0x11c,PC)                ; 0x00E322F4 "unmap%$"
 *   00e321da    bsr.w 0x00e31ec8
 *   00e321de    addq.w #0x4,SP
 *   00e321e0    tst.b D0b
 *   00e321e2    bmi.w 0x00e3227e
 *   00e321e6    pea (-0x50,A6)                ; status
 *   00e321ea    pea (-0x41e,PC)               ; &0x00E31DCE mode 1
 *   00e321ee    pea (-0x38,A6)                ; &file_uid
 *   00e321f2    jsr 0x00e5fcfc.l              ; FILE_$UNLOCK
 *   00e321f8    lea (0xc,SP),SP
 *   00e321fc    pea (0xee,PC)                 ; 0x00E322EC "unlock%$"
 *   00e32200    bsr.w 0x00e31ec8
 *   00e32204    addq.w #0x4,SP
 *   00e32206    tst.b D0b
 *   00e32208    bmi.b 0x00e3227e
 *   00e3220a    pea (-0x54,A6)                ; &wire_count1
 *   00e3220e    pea (0x8a,PC)                 ; &0x00E3229A (10)
 *   00e32212    pea (-0x28,A6)                ; wired_pages
 *   00e32216    pea (0xd0,PC)                 ; &0x00E322E8 PEB_$WIRED_PROC_END
 *   00e3221a    pea (0xc8,PC)                 ; &0x00E322E4 PEB_$WIRED_PROC_START
 *   00e3221e    jsr 0x00e44ba4.l              ; MST_$WIRE_AREA
 *   00e32224    lea (0x14,SP),SP
 *   00e32228    move.w (-0x54,A6),D2w
 *   00e3222c    pea (-0x56,A6)                ; &wire_count2
 *   00e32230    moveq #0xa,D0
 *   00e32232    sub.w D2w,D0w
 *   00e32234    move.w D0w,(-0x62,A6)         ; wire_remaining = 10 - wire_count1
 *   00e32238    pea (-0x62,A6)
 *   00e3223c    move.w D2w,D1w
 *   00e3223e    lsl.w #0x2,D1w
 *   00e32240    pea (-0x28,A6,D1w*0x1)        ; &wired_pages[wire_count1]
 *   00e32244    pea (0x9a,PC)                 ; &0x00E322E0 PEB_$WIRED_DATA_END
 *   00e32248    pea (0x92,PC)                 ; &0x00E322DC PEB_$WIRED_DATA_START
 *   00e3224c    jsr 0x00e44ba4.l              ; MST_$WIRE_AREA (no pop; unlk)
 *   00e32252    movea.l #0xe24c78,A0
 *   00e32258    move.w (0x18,A0),(-0x52,A6)   ; ctl = shadow
 *   00e3225e    moveq #0xd,D1
 *   00e32260    or.w D1w,(-0x52,A6)           ; ctl |= 0xD
 *   00e32264    move.w (-0x52,A6),(0x18,A0)   ; shadow = ctl
 *   00e3226a    move.b (0x00ff73fc).l,(-0x58,A6) ; read byte 0x3FC of the ctl page
 *   00e32272    move.w (-0x52,A6),(0x00ff7000).l ; PEB_CTL = ctl
 *   00e3227a    st (0x1b,A0)                  ; wcs_loaded = true
 *   00e3227e    movem.l (-0x74,A6),{D2 D3 A2 A3}
 *   00e32284    unlk A6
 *   00e32286    rts
 */
void PEB_$LOAD_WCS(void)
{
    uint32_t wired_pages[10];          /* -0x28 */
    peb_wcs_entry_t verify;            /* -0x30 */
    uid_t file_uid;                    /* -0x38 */
    uint32_t maps_length;              /* -0x40 */
    uint32_t map_va;                   /* -0x44 */
    uint8_t lock_info[4];              /* -0x48 */
    uint32_t map_info;                 /* -0x4C */
    status_$t status;                  /* -0x50 */
    uint16_t ctl;                      /* -0x52 */
    uint16_t wire_count1;              /* -0x54 */
    uint16_t wire_count2;              /* -0x56 */
    uint8_t ctl_page_byte;             /* -0x58 */
    uint16_t wire_remaining;           /* -0x62 */
    peb_wcs_header_t *wcs_data;        /* A2 */
    peb_wcs_entry_t *entries;          /* A3 + 4 */
    int16_t last;                      /* D2: dbf counter */
    int16_t i;                         /* D3 */
    m68k_ptr_t save_va;                /* A0 in the 19-step loop */
    int16_t n;

    /* 0x00E31FE0-0x00E31FEE */
    if ((int8_t)PEB_$M68881_SAVE_FLAG < 0 || (int8_t)PEB_$SAVEP_FLAG < 0) {
        /* 0x00E31FF2-0x00E32016 */
        FILE_$CREATE(&UID_$NIL, &file_uid, &status);
        if (PEB_$LOAD_WCS_CHECK_ERR(peb_$msg_create_file, &status) < 0) {
            return;
        }

        /* 0x00E3201A-0x00E32048 */
        FILE_$LOCK(&file_uid, &peb_$lock_index, &peb_$lock_mode_4,
                   &peb_$lock_rights, lock_info, &status);
        if (PEB_$LOAD_WCS_CHECK_ERR(peb_$msg_lock_file, &status) < 0) {
            return;
        }

        /* 0x00E3204C-0x00E3208E */
        maps_length = 0x497A;
        FP_$SAVEP = ARCH_PTR_TO_VA(MST_$MAPS(0, true, &file_uid, 0, maps_length,
                                             0x16, 0, true, &map_info, &status));
        if (PEB_$LOAD_WCS_CHECK_ERR(peb_$msg_map_file, &status) < 0) {
            return;
        }

        /*
         * 0x00E32092-0x00E320A0: nineteen `lea (0x400,A0),A0` steps from the
         * save-area address.  No memory is touched and the result is
         * dropped; reproduced as the address arithmetic it is.
         */
        save_va = FP_$SAVEP;
        for (n = 0; n < 19; n++) {
            save_va += 0x400;
        }
        (void)save_va;
        return;
    }

    /* 0x00E320A8-0x00E320AE */
    if ((int8_t)PEB_$INSTALLED >= 0) {
        return;
    }

    /* 0x00E320B2-0x00E320D8 */
    NAME_$RESOLVE(peb_$microcode_path, &peb_$path_len, &file_uid, &status);
    if (PEB_$LOAD_WCS_CHECK_ERR(peb_$msg_resolve, &status) < 0) {
        return;
    }

    /* 0x00E320DC-0x00E3210A */
    FILE_$LOCK(&file_uid, &peb_$lock_index, &peb_$const_word_1,
               &peb_$lock_rights, lock_info, &status);
    if (PEB_$LOAD_WCS_CHECK_ERR(peb_$msg_lock, &status) < 0) {
        return;
    }

    /* 0x00E3210E-0x00E32146 */
    wcs_data = (peb_wcs_header_t *)MST_$MAP(&file_uid, (uint32_t *)&peb_$zero_long,
                                            (uint32_t *)&peb_$map_length,
                                            (uint16_t *)&peb_$map_mode,
                                            (uint32_t *)&peb_$zero_long,
                                            (uint8_t *)&peb_$lock_rights,
                                            &map_info, &status);
    if (PEB_$LOAD_WCS_CHECK_ERR(peb_$msg_map, &status) < 0) {
        return;
    }
    entries = (peb_wcs_entry_t *)(void *)((uint8_t *)wcs_data + 4);

    /* 0x00E3214A-0x00E3216E: write every entry (dbf on count-1) */
    last = (int16_t)(wcs_data->entry_count - 1);
    if (last >= 0) {
        for (i = 0; i <= last; i++) {
            peb_$write_wcs((uint16_t)(wcs_data->start_addr + i), &entries[i]);
        }
    }

    /* 0x00E32172-0x00E321B4: read each one back; two cmpm.l compares */
    last = (int16_t)(wcs_data->entry_count - 1);
    if (last >= 0) {
        for (i = 0; i <= last; i++) {
            peb_$read_wcs((uint16_t)(wcs_data->start_addr + i), &verify);
            if (verify.word0 != entries[i].word0 ||
                verify.word1 != entries[i].word1 ||
                verify.word2 != entries[i].word2) {
                CRASH_SYSTEM(&PEB_WCS_Verify_Failed_Err);
            }
        }
    }

    /* 0x00E321B8-0x00E321E2 */
    map_va = ARCH_PTR_TO_VA(wcs_data);
    MST_$UNMAP(&file_uid, &map_va, &map_info, &status);
    if (PEB_$LOAD_WCS_CHECK_ERR(peb_$msg_unmap, &status) < 0) {
        return;
    }

    /* 0x00E321E6-0x00E32208 */
    FILE_$UNLOCK(&file_uid, &peb_$const_word_1, &status);
    if (PEB_$LOAD_WCS_CHECK_ERR(peb_$msg_unlock, &status) < 0) {
        return;
    }

    /* 0x00E3220A-0x00E3224C: wire the PEB code, then the data, into one list */
    MST_$WIRE_AREA(&peb_$wired_proc_start, &peb_$wired_proc_end,
                   wired_pages, &peb_$wire_max_pages, &wire_count1);
    wire_remaining = (uint16_t)(10 - wire_count1);
    MST_$WIRE_AREA(&peb_$wired_data_start, &peb_$wired_data_end,
                   &wired_pages[wire_count1], &wire_remaining, &wire_count2);

    /* 0x00E32252-0x00E3227A */
    ctl = PEB_$CTL_SHADOW;
    ctl |= PEB_CTL_ENABLE;
    PEB_$CTL_SHADOW = ctl;
    ctl_page_byte = PEB_CTL_PAGE_BYTE_3FC;
    (void)ctl_page_byte;
    PEB_CTL = ctl;
    PEB_$WCS_LOADED = 0xFF;
}
