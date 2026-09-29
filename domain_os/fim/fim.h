/*
 * FIM - Fault/Interrupt Manager Module
 *
 * Provides fault and interrupt handling for Domain/OS.
 * Manages CPU exceptions, signal delivery, cleanup handlers,
 * floating point state, and quit processing.
 *
 * This module is heavily architecture-specific (m68k/68010+)
 * with most functions implemented in assembly.
 */

#ifndef FIM_H
#define FIM_H

#include "base/base.h"
#include "ec/ec.h"
#include "parity/parity.h"      /* parity_state_t, the first cell of FIM_$WIRED_DATA */
#include "proc1/proc1_config.h"   /* PROC1_MAX_PROCESSES, for FIM_$CLEANUP_STACK */

/*
 * ============================================================================
 * Constants
 * ============================================================================
 */

/* Lock IDs used by FIM */
#define FIM_WIRED_LOCK_ID       0x0D
#define FIM_UNWIRED_LOCK_ID     0x03

/* Delivery frame flags (offset 0x4E in delivery frame) */
#define FIM_DF_FLAG_NEGATIVE      0x80    /* Bit 7: Negative status */
#define FIM_DF_FLAG_SUPERVISOR    0x40    /* Bit 6: Was in supervisor mode */
#define FIM_DF_FLAG_TRACE         0x20    /* Bit 5: Trace fault */
#define FIM_DF_FLAG_BUS_ERROR     0x10    /* Bit 4: Bus error */
#define FIM_DF_FLAG_RESTORE_FP    0x08    /* Bit 3: Need to restore FP state */
#define FIM_DF_FLAG_IN_FIM        0x04    /* Bit 2: In FIM (always set) */
#define FIM_DF_FLAG_FP_SAVED      0x02    /* Bit 1: FP state saved */
#define FIM_DF_FLAG_CLEANUP_RAN   0x01    /* Bit 0: Cleanup handler ran */

/* Exception vector format codes (m68010+) */
#define FIM_FRAME_FORMAT_SHORT  0x0     /* 4-word frame */
#define FIM_FRAME_FORMAT_THROW  0x1     /* Throwaway frame */
#define FIM_FRAME_FORMAT_INSTR  0x2     /* Instruction exception */
#define FIM_FRAME_FORMAT_COPROC 0x9     /* Coprocessor mid-instruction */
#define FIM_FRAME_FORMAT_SHORT_BUS 0xA  /* Short bus cycle fault */
#define FIM_FRAME_FORMAT_LONG_BUS  0xB  /* Long bus cycle fault */

/*
 * FIM_AS_COUNT - number of address spaces the FIM per-AS tables are sized for
 *
 * Every per-AS table in the FIM module holds exactly 58 (0x3A) elements.
 * The tables are laid out back to back in the module's data area, so each
 * one's element count is pinned by the address of the next object; the whole
 * chain closes on known code and data addresses:
 *
 *   0x00E2126C FIM_IN_FIM          58 * 1  -> 0x00E212A6 (+2 align)
 *   0x00E212A8 FIM_$USER_FIM_ADDR  58 * 4  -> 0x00E21390 FIM_$DATA.frame_size
 *   0x00E21890 FIM_$TRACE_BIT      58 * 1  -> 0x00E218CA JMP_TO_BUS_ERR
 *   0x00E22002 FIM_$QUIT_EC        58 * 12 -> 0x00E222BA FIM_$QUIT_VALUE
 *   0x00E222BA FIM_$QUIT_VALUE     58 * 4  -> 0x00E223A2 FIM_$TRACE_STS
 *   0x00E223A2 FIM_$TRACE_STS      58 * 4  -> 0x00E2248A FIM_$QUIT_INH
 *   0x00E2248A FIM_$QUIT_INH       58 * 1  -> 0x00E224C4 FIM_$DELIV_EC
 *   0x00E224C4 FIM_$DELIV_EC       58 * 12 -> 0x00E2277C FIM_$GET_USER_SR_PTR
 *
 * (Map names.  The tables are fields of FIM_$DATA and FIM_$WIRED_DATA
 * below, except FIM_$TRACE_BIT, which fim/sau2/trace_bit.s defines.)
 *
 * The base addresses are the ones the code itself materialises: 0x00E2126C
 * and its +0x3C displacement from FIM_$INSTALL (0x00E0A9C2) and
 * FIM_$FREE_ASID (0x00E0AA6C); 0x00E22002/0x00E222BA/0x00E2248A from
 * FIM_$INIT_ASID (0x00E0AA24); 0x00E224C4 from FIM_$ACKNOWLEDGE (0x00E0A96C);
 * 0x00E21890 from the "lea (-0x1002,PC),A1" at the head of
 * FIM_$CLEAR_TRACE_FAULT (0x00E22890).
 */
#define FIM_AS_COUNT            58

/* FP save area types */
#define FIM_FP_TYPE_NULL        0       /* Null state (no FP context) */
#define FIM_FP_TYPE_IDLE        1       /* Idle state */
#define FIM_FP_TYPE_BUSY        2       /* Busy state */

/*
 * ============================================================================
 * Data Structures
 * ============================================================================
 */

/*
 * m68010 exception frame - varies by format
 * Format code is in bits 15:12 of the status register extension word
 */
/* PACKED: m68k aligns 32-bit fields to 2 bytes, so the recovered offsets
 * below are only reproducible on a 4/8-byte-aligning host if the record is
 * packed.  Packing changes no m68k layout. */
typedef struct fim_exception_frame_t {
    uint16_t    sr;             /* 0x00: Status register */
    uint32_t    pc;             /* 0x02: Program counter */
    uint16_t    format_vector;  /* 0x06: Format code (bits 15:12) and vector (bits 11:0) */
    /* Additional words depend on format code - see frame tables */
} __attribute__((packed)) fim_exception_frame_t;

/* Layout recovered from the disassembly -- see the field comments above. */
_Static_assert(__builtin_offsetof(fim_exception_frame_t, sr) == 0x00, "fim_exception_frame_t.sr");
_Static_assert(__builtin_offsetof(fim_exception_frame_t, pc) == 0x02, "fim_exception_frame_t.pc");
_Static_assert(__builtin_offsetof(fim_exception_frame_t, format_vector) == 0x06, "fim_exception_frame_t.format_vector");

/*
 * Long bus cycle fault frame (format 0xB, 68010)
 * This is the largest exception frame format
 */
/* PACKED: m68k aligns 32-bit fields to 2 bytes, so the recovered offsets
 * below are only reproducible on a 4/8-byte-aligning host if the record is
 * packed.  Packing changes no m68k layout. */
typedef struct fim_long_bus_frame_t {
    uint16_t    sr;             /* 0x00: Status register */
    uint32_t    pc;             /* 0x02: Program counter */
    uint16_t    format_vector;  /* 0x06: Format/vector word */
    uint16_t    ssw;            /* 0x08: Special status word */
    /* Additional fields for address, data, etc. */
} __attribute__((packed)) fim_long_bus_frame_t;

/* Layout recovered from the disassembly -- see the field comments above. */
_Static_assert(__builtin_offsetof(fim_long_bus_frame_t, sr) == 0x00, "fim_long_bus_frame_t.sr");
_Static_assert(__builtin_offsetof(fim_long_bus_frame_t, pc) == 0x02, "fim_long_bus_frame_t.pc");
_Static_assert(__builtin_offsetof(fim_long_bus_frame_t, format_vector) == 0x06, "fim_long_bus_frame_t.format_vector");
_Static_assert(__builtin_offsetof(fim_long_bus_frame_t, ssw) == 0x08, "fim_long_bus_frame_t.ssw");

/*
 * FIM delivery frame - created on user stack for fault delivery
 * Total size: 0x6A bytes
 *
 * This structure is built by FIM_$BUILD_DF and contains all the
 * context needed to deliver a fault to user mode.
 */
/* PACKED: m68k aligns 32-bit fields to 2 bytes, so the recovered offsets
 * below are only reproducible on a 4/8-byte-aligning host if the record is
 * packed.  Packing changes no m68k layout. */
typedef struct fim_delivery_frame_t {
    uint16_t    magic;          /* 0x00: Magic number 0xDFDF */
    uint32_t    status;         /* 0x02: Fault status code */
    /* 0x06-0x41: Saved registers D0-D7, A0-A6 (15 longs = 60 bytes) */
    uint32_t    regs[15];       /* 0x06: D0-D7, A0-A5, A6 */
    uint32_t    pc;             /* 0x42: Saved PC */
    uint32_t    fault_info1;    /* 0x46: Fault-specific info */
    uint32_t    fault_info2;    /* 0x4A: Fault-specific info */
    uint8_t     flags;          /* 0x4E: Delivery frame flags */
    uint8_t     version;        /* 0x4F: Frame version (2) */
    uint32_t    reserved1;      /* 0x50: Reserved */
    uint16_t    orig_sr;        /* 0x54: Original SR from exception */
    uint32_t    orig_pc;        /* 0x56: Original PC from exception */
    uint16_t    orig_sr2;       /* 0x5A: User SR */
    uint32_t    fp_save_ptr;    /* 0x5C: Pointer to FP state (or 0) */
    uint16_t    param3;         /* 0x60: Signal parameter 3 */
    uint32_t    param4;         /* 0x62: Signal parameter 4 */
    uint32_t    user_pc;        /* 0x66: User program counter */
} __attribute__((packed)) fim_delivery_frame_t;

/* Layout recovered from the disassembly -- see the field comments above. */
_Static_assert(__builtin_offsetof(fim_delivery_frame_t, magic) == 0x00, "fim_delivery_frame_t.magic");
_Static_assert(__builtin_offsetof(fim_delivery_frame_t, status) == 0x02, "fim_delivery_frame_t.status");
_Static_assert(__builtin_offsetof(fim_delivery_frame_t, regs) == 0x06, "fim_delivery_frame_t.regs");
_Static_assert(__builtin_offsetof(fim_delivery_frame_t, pc) == 0x42, "fim_delivery_frame_t.pc");
_Static_assert(__builtin_offsetof(fim_delivery_frame_t, fault_info1) == 0x46, "fim_delivery_frame_t.fault_info1");
_Static_assert(__builtin_offsetof(fim_delivery_frame_t, fault_info2) == 0x4A, "fim_delivery_frame_t.fault_info2");
_Static_assert(__builtin_offsetof(fim_delivery_frame_t, flags) == 0x4E, "fim_delivery_frame_t.flags");
_Static_assert(__builtin_offsetof(fim_delivery_frame_t, version) == 0x4F, "fim_delivery_frame_t.version");
_Static_assert(__builtin_offsetof(fim_delivery_frame_t, reserved1) == 0x50, "fim_delivery_frame_t.reserved1");
_Static_assert(__builtin_offsetof(fim_delivery_frame_t, orig_sr) == 0x54, "fim_delivery_frame_t.orig_sr");
_Static_assert(__builtin_offsetof(fim_delivery_frame_t, orig_pc) == 0x56, "fim_delivery_frame_t.orig_pc");
_Static_assert(__builtin_offsetof(fim_delivery_frame_t, orig_sr2) == 0x5A, "fim_delivery_frame_t.orig_sr2");
_Static_assert(__builtin_offsetof(fim_delivery_frame_t, fp_save_ptr) == 0x5C, "fim_delivery_frame_t.fp_save_ptr");
_Static_assert(__builtin_offsetof(fim_delivery_frame_t, param3) == 0x60, "fim_delivery_frame_t.param3");
_Static_assert(__builtin_offsetof(fim_delivery_frame_t, param4) == 0x62, "fim_delivery_frame_t.param4");
_Static_assert(__builtin_offsetof(fim_delivery_frame_t, user_pc) == 0x66, "fim_delivery_frame_t.user_pc");

/*
 * FIM per-process cleanup handler entry
 * Stored in a stack per address space
 */
typedef struct fim_cleanup_entry_t {
    struct fim_cleanup_entry_t *next;   /* Link to previous handler */
    void        *handler;       /* Handler function */
    void        *context;       /* Handler context */
} fim_cleanup_entry_t;

/*
 * Register save area used by various FIM functions
 */
typedef struct fim_regs_t {
    uint32_t    d[8];           /* D0-D7 */
    uint32_t    a[7];           /* A0-A6 */
    uint32_t    usp;            /* User stack pointer */
} fim_regs_t;

/* No offsets were recovered for fim_regs_t -- it is a plain register save
 * area whose layout follows from the declared widths -- so it carries no
 * layout _Static_asserts. */

/*
 * Signal context structure (BSD m68k sigcontext)
 *
 * Passed by the signal trampoline to PROC2_$SIGRETURN and then
 * to FIM_$FAULT_RETURN.  Layout confirmed from assembly offsets
 * in FIM_$FAULT_RETURN (0x08, 0x0C, 0x10, 0x14, 0x18).
 */
typedef struct sigcontext_t {
    int32_t     sc_onstack;     /* 0x00: Non-zero if on signal stack */
    uint32_t    sc_mask;        /* 0x04: Signal mask to restore */
    uint32_t    sc_sp;          /* 0x08: User stack pointer */
    uint32_t    sc_fp;          /* 0x0C: Frame pointer (A6) */
    uint32_t    sc_ap;          /* 0x10: Argument pointer (A5) */
    uint32_t    sc_pc;          /* 0x14: Program counter */
    uint32_t    sc_ps;          /* 0x18: Status register in the LOW word
                                 * (0x1A): FIM_$FAULT_RETURN reads the
                                 * longword (0x00E2184A move.l (0x18,A2),D0;
                                 * and.w #0xC01F,D0; move.w D0,-(A0)), as
                                 * BSD's `int sc_ps' (source-hf8q) */
} sigcontext_t;

/* Layout recovered from the disassembly -- see the field comments above. */
_Static_assert(__builtin_offsetof(sigcontext_t, sc_onstack) == 0x00, "sigcontext_t.sc_onstack");
_Static_assert(__builtin_offsetof(sigcontext_t, sc_mask) == 0x04, "sigcontext_t.sc_mask");
_Static_assert(__builtin_offsetof(sigcontext_t, sc_sp) == 0x08, "sigcontext_t.sc_sp");
_Static_assert(__builtin_offsetof(sigcontext_t, sc_fp) == 0x0C, "sigcontext_t.sc_fp");
_Static_assert(__builtin_offsetof(sigcontext_t, sc_ap) == 0x10, "sigcontext_t.sc_ap");
_Static_assert(__builtin_offsetof(sigcontext_t, sc_pc) == 0x14, "sigcontext_t.sc_pc");
_Static_assert(__builtin_offsetof(sigcontext_t, sc_ps) == 0x18, "sigcontext_t.sc_ps");
_Static_assert(sizeof(sigcontext_t) == 0x1C, "sigcontext_t size");

/*
 * ============================================================================
 * Global Data
 * ============================================================================
 */

/*
 * ----------------------------------------------------------------------------
 * FIM_$DATA - the FIM_ module data block (map "D E2126C FIM_ size = 134")
 * ----------------------------------------------------------------------------
 *
 * Module data blocks FIM_$DATA and FIM_$WIRED_DATA: Claude Opus 5.5
 * (source-l2yd).
 *
 * The A5 block of the Pascal FIM routines: FIM_$INSTALL (0x00E0A9C2),
 * FIM_$FREE_ASID (0x00E0AA6C), FIM_$GET_FIM_ADDR and FIM_$BUILD_DF
 * (0x00E0A458) load A5 with 0xE2126C.  A MODULE_DATA block linked in the
 * SAU2 map's order after EC2_ASM and before FIM_UNWIRED; the address is the
 * ordering key, not the link address.  The map names two objects in it:
 *
 *   A5 off  image      field
 *   0x000   0xE2126C   in_fim[0..57]         map FIM_IN_FIM
 *   0x03A   0xE212A6   (pad)
 *   0x03C   0xE212A8   user_fim_addr[0..57]  map FIM_$USER_FIM_ADDR
 *   0x124   0xE21390   frame_size[16]        exception frame sizes by format
 *
 * Both per-ASID tables are [0..57] (FIM_AS_COUNT) with element 0 at the map
 * symbol, indexed with PROC1_$AS_ID as the code does - `move.l
 * (0x3c,A5,D2w*0x1),D1' with D2 = asid*4 (FIM_$INSTALL), `clr.l
 * (0x3c,A5,D0w*0x1)' (FIM_$FREE_ASID 0x00E0AA8E) - so there is no bias
 * slot.  user_fim_addr holds the user-mode handler addresses FIM_$INSTALL
 * swaps; the pointer width moves frame_size on a host, so the offsets past
 * in_fim are asserted for the target only.
 *
 * frame_size is the table FIM_$BUILD_DF indexes by the exception frame's
 * format code (0x00E0A492, 0x00E0A668, 0x00E0A916).  Image bytes at
 * 0x00E21390 (`gsk read 0xE2126C 308'): 08 08 0c 00 10 00 00 00 3a 14 20 5c
 * 00 00 00 00; everything before it is zero.
 */
#define FIM_$DATA_SIZE 0x134            /* map: FIM_ size = 134 */
#define FIM_FRAME_FORMAT_COUNT 16

typedef struct fim_$data_t {
    int8_t      in_fim[FIM_AS_COUNT];           /* +0x000: 0 = not in FIM,
                                                 *   0xFF = in FIM, < 0 blocked */
    uint16_t    _003a;                          /* +0x03A: pad */
    void       *user_fim_addr[FIM_AS_COUNT];    /* +0x03C: user FIM handler */
    uint8_t     frame_size[FIM_FRAME_FORMAT_COUNT]; /* +0x124: bytes per frame
                                                 *   format code */
} fim_$data_t;

_Static_assert(__builtin_offsetof(fim_$data_t, in_fim) == 0x000, "FIM_IN_FIM (0,A5)");
_Static_assert(__builtin_offsetof(fim_$data_t, in_fim[1]) == 0x001, "in_fim stride 1");
#if defined(ARCH_M68K)
/* Target only: user_fim_addr[] holds pointers. */
_Static_assert(__builtin_offsetof(fim_$data_t, user_fim_addr) == 0x03C, "FIM_$USER_FIM_ADDR (0x3c,A5)");
_Static_assert(__builtin_offsetof(fim_$data_t, user_fim_addr[1]) == 0x040, "user_fim_addr stride 4 (asid*4)");
_Static_assert(__builtin_offsetof(fim_$data_t, frame_size) == 0x124, "frame size table 0xE21390");
_Static_assert(sizeof(fim_$data_t) == FIM_$DATA_SIZE, "FIM_: map size 0x134");
#endif

MODULE_DATA_DECLARE(fim_$data_t, FIM_$DATA, 0x00E2126C);

/*
 * ----------------------------------------------------------------------------
 * FIM_$WIRED_DATA - the data run of the FIM_WIRED segment, 0xE21FE6..0xE2277C
 * ----------------------------------------------------------------------------
 *
 * The map segment "D E21890 FIM_WIRED size = 1074" is the hand-written
 * wired FIM code (fim/sau2/*.s, fp/sau2/*.s, one section per routine) with data
 * interleaved.  The cells between FIM_$PARITY_TRAP (ends 0xE21FE6) and
 * FIM_$GET_USER_SR_PTR (0xE2277C) are one unbroken run of data that the C
 * code and other modules address by absolute address; that run is this
 * block, keyed at the map's PARITY_$INFO.  The cells inside the code
 * (FIM_$TRACE_BIT, FP_$SAVEP/OWNER/EXCLUSION, BUS_ERROR_SWITCH) stay with
 * the assembly that holds them.
 *
 *   off     image      field                 map name
 *   0x000   0xE21FE6   parity                PARITY_$INFO (parity_state_t;
 *                                            PARITY_$CHK `movea.l #0xe21fe6,A2')
 *   0x014   0xE21FFA   miss_status           MISS_STATUS
 *   0x018   0xE21FFE   pending_trace_faults  PENDING_TRACE_FAULTS ((0x76E,A1)
 *                                            in FIM_$CLEAR_TRACE_FAULT)
 *   0x01C   0xE22002   quit_ec[0..57]        FIM_$QUIT_EC
 *   0x2D4   0xE222BA   quit_value[0..57]     FIM_$QUIT_VALUE
 *   0x3BC   0xE223A2   trace_sts[0..57]      FIM_$TRACE_STS
 *   0x4A4   0xE2248A   quit_inh[0..57]       FIM_$QUIT_INH
 *   0x4DE   0xE224C4   deliv_ec[0..57]       FIM_$DELIV_EC
 *
 * Every table is per-ASID, [0..57] (FIM_AS_COUNT) with element 0 at its map
 * symbol and indexed with the asid itself, e.g. FIM_$INIT_ASID
 * (0x00E0AA3A..0x00E0AA60): D0 = asid*4 + asid*8 (`lsl.w #0x2' / `add.w'
 * / `add.w'), `movea.l #0xe22002,A0', D1 = asid*4, `movea.l #0xe222ba,A1',
 * `move.l (0x0,A0,D0w*0x1),(0x0,A1,D1w*0x1)', then `movea.l #0xe2248a,A1'
 * / `st (0x0,A1,D2w*0x1)' with D2 = asid.  So quit_ec and deliv_ec have a
 * 12-byte stride (ec_$eventcount_t), quit_value and trace_sts 4, quit_inh 1;
 * FIM_$ACKNOWLEDGE `pea (0,A1,D2)' (0x00E0A9AA) and PROC2_$GET_EC
 * (`movea.l #0xe224c4,A1', D0 = asid*12, 0x00E40118) reach deliv_ec the
 * same way.  No table has a bias slot.
 *
 * Image contents (`gsk read 0xE21FE6 1942', in pieces): each eventcount in
 * quit_ec[] and deliv_ec[] ships as value 0 with both waiter links pointing
 * at itself (e.g. 0xE22002: 00000000 00e22002 00e22002), and quit_inh[] is
 * 0xFF throughout (0xE2248A..0xE224C3); every other byte is zero.  The
 * eventcounts carry pointers, so offsets past pending_trace_faults are
 * asserted for the target only.
 *
 * The fim/sau2 routines reach parity, pending_trace_faults, trace_sts and
 * quit_inh by name through `.set' aliases onto this block.
 */
#define FIM_$WIRED_DATA_SIZE 0x796      /* 0xE21FE6..0xE2277C */

typedef struct fim_$wired_data_t {
    parity_state_t   parity;                    /* +0x000 PARITY_$INFO */
    uint32_t         miss_status;               /* +0x014 MISS_STATUS */
    uint32_t         pending_trace_faults;      /* +0x018 address spaces with a
                                                 *   pending trace fault */
    ec_$eventcount_t quit_ec[FIM_AS_COUNT];     /* +0x01C quit eventcounts */
    uint32_t         quit_value[FIM_AS_COUNT];  /* +0x2D4 last quit_ec value
                                                 *   acknowledged */
    status_$t        trace_sts[FIM_AS_COUNT];   /* +0x3BC trace fault status */
    int8_t           quit_inh[FIM_AS_COUNT];    /* +0x4A4 quit inhibited */
    ec_$eventcount_t deliv_ec[FIM_AS_COUNT];    /* +0x4DE signal delivery
                                                 *   eventcounts */
} fim_$wired_data_t;

_Static_assert(__builtin_offsetof(fim_$wired_data_t, parity) == 0x000, "PARITY_$INFO 0xE21FE6");
_Static_assert(__builtin_offsetof(fim_$wired_data_t, miss_status) == 0x014, "MISS_STATUS 0xE21FFA");
_Static_assert(__builtin_offsetof(fim_$wired_data_t, pending_trace_faults) == 0x018,
               "PENDING_TRACE_FAULTS 0xE21FFE");
#if defined(ARCH_M68K)
/* Target only: ec_$eventcount_t carries two waiter pointers. */
_Static_assert(__builtin_offsetof(fim_$wired_data_t, quit_ec) == 0x01C, "FIM_$QUIT_EC 0xE22002");
_Static_assert(__builtin_offsetof(fim_$wired_data_t, quit_ec[1]) == 0x028, "quit_ec stride 12 (asid*4 + asid*8)");
_Static_assert(__builtin_offsetof(fim_$wired_data_t, quit_value) == 0x2D4, "FIM_$QUIT_VALUE 0xE222BA");
_Static_assert(__builtin_offsetof(fim_$wired_data_t, quit_value[1]) == 0x2D8, "quit_value stride 4 (asid*4)");
_Static_assert(__builtin_offsetof(fim_$wired_data_t, trace_sts) == 0x3BC, "FIM_$TRACE_STS 0xE223A2");
_Static_assert(__builtin_offsetof(fim_$wired_data_t, trace_sts[1]) == 0x3C0, "trace_sts stride 4 (asid*4)");
_Static_assert(__builtin_offsetof(fim_$wired_data_t, quit_inh) == 0x4A4, "FIM_$QUIT_INH 0xE2248A");
_Static_assert(__builtin_offsetof(fim_$wired_data_t, quit_inh[1]) == 0x4A5, "quit_inh stride 1");
_Static_assert(__builtin_offsetof(fim_$wired_data_t, deliv_ec) == 0x4DE, "FIM_$DELIV_EC 0xE224C4");
_Static_assert(__builtin_offsetof(fim_$wired_data_t, deliv_ec[1]) == 0x4EA, "deliv_ec stride 12 (asid*12)");
_Static_assert(sizeof(fim_$wired_data_t) == FIM_$WIRED_DATA_SIZE,
               "deliv_ec[57] ends at FIM_$GET_USER_SR_PTR 0xE2277C");
#endif

MODULE_DATA_DECLARE(fim_$wired_data_t, FIM_$WIRED_DATA, 0x00E21FE6);

/*
 * FIM_$INITIAL_STACK_SIZE - Bytes reserved above a new process's startup
 * context on its initial stack (used by PROC2_$CREATE / PROC2_$FORK, which
 * read it absolutely: `add.l (0x00e21824).l,D0' at 0x00E7286E).
 * Address: 0x00E21824 (4 bytes, value 8), a cell inside the FIM_UNWIRED
 * code segment between FIM_$SINGLE_STEP and FIM_$FAULT_RETURN, so it stays
 * an individual object (fim/fim_data.c) like the other exported cells of
 * hand-written segments (ordering them is source-91vs).
 */
extern uint32_t FIM_$INITIAL_STACK_SIZE;

/*
 * FIM_$SPUR_CNT - spurious interrupts taken since boot
 * Address: 0x00E21F7E, a cell of the FIM_WIRED code right after
 * FIM_$SPURIOUS_INT, which counts into it off its module base; defined with
 * it in fim/sau2/spurious_int.s (source-kt66).
 */
extern uint32_t FIM_$SPUR_CNT;

/*
 * FIM_$COLD_BUS_ERR - Bus error handler for cold boot
 * Address 0x00e35004
 */
extern void FIM_$COLD_BUS_ERR(void);

/*
 * ============================================================================
 * Function Prototypes - C Functions
 * ============================================================================
 */

/*
 * FIM_$GET_FIM_ADDR - Return the current AS's user-mode fault handler
 *
 * TRAP #0 subcode 0x01.  Returns FIM_$USER_FIM_ADDR[PROC1_$AS_ID].
 *
 * Address: 0x00e0aa04
 */
void *FIM_$GET_FIM_ADDR(void);

/*
 * FIM_$INSTALL - Install a user-mode fault handler for the current AS
 *
 * TRAP #1 subcode 0x02.  `new_addr` is a by-reference longword holding the
 * new handler address; the previous handler is returned.  Installing the
 * first handler for an address space clears FIM_$QUIT_INH for that AS.
 *
 * Address: 0x00e0a9c2
 */
void *FIM_$INSTALL(void **new_addr);

/*
 * FIM_$ACKNOWLEDGE - Advance signal delivery mechanism
 *
 * Updates the quit value for the current address space from the quit
 * event counter, clears the quit inhibit flag, and advances the
 * delivery event counter to signal that signal delivery can proceed.
 *
 * Called during signal acknowledge and signal delivery operations.
 *
 * Address: 0x00e0a96c
 *
 * The name comes from the SR10.4 kernel link maps
 * (sr10.4-install/sau7/domain_os.map), which list the FIM_ module's entries
 * in address order: INIT_FF_POOL, DISPOSE_FF, RESTORE_FF, BUILD_DF,
 * ACKNOWLEDGE, INSTALL, GET_FIM_ADDR, INIT_PID, FREE_PID, GET_USER_PC.  In
 * the SAU2 image FIM_$BUILD_DF is 0x00E0A458 and the next four entries are
 * 0x00E0A96C, 0x00E0A9C2 (INSTALL), 0x00E0AA04 (GET_FIM_ADDR) and
 * 0x00E0AA24 (INIT_PID), which pins this one as FIM_$ACKNOWLEDGE.  Until
 * bead source-y6s0 the tree carried it under the descriptive name it had
 * been given here, spelled the way this file used to be named:
 * fim/advance_signal_delivery.c.
 */
void FIM_$ACKNOWLEDGE(void);

/*
 * FIM_$INIT_ASID - Reset the per-PID quit state for a newly created process
 *
 * Takes the ADDRESS of the pid word (PROC2_$INIT_ENTRY_INTERNAL passes
 * &entry->asid at 0x00E73310).  Clears the trace fault, seeds
 * FIM_$QUIT_VALUE[pid] from the head longword of FIM_$QUIT_EC[pid] and sets
 * FIM_$QUIT_INH[pid].
 *
 * The "pid" is the address-space id: the caller passes &proc2_info_t.asid,
 * and every table this function touches is one of the FIM per-AS tables
 * (FIM_AS_COUNT entries each).
 *
 * Address: 0x00e0aa24
 */
void FIM_$INIT_ASID(int16_t *pid);

/*
 * FIM_$FREE_ASID - Release the per-PID FIM state for a dying process
 *
 * Counterpart of FIM_$INIT_ASID, with the same by-reference word argument
 * (PROC2_$DELETE_CLEANUP passes a word local at 0x00E749D6).  Clears the
 * trace fault, drops FIM_$USER_FIM_ADDR[pid] and sets FIM_$QUIT_INH[pid],
 * so the address space is back to "quits inhibited, no handler installed".
 *
 * Address: 0x00e0aa6c
 */
void FIM_$FREE_ASID(int16_t *pid);

/*
 * FIM_$GET_USER_PC - longword at the top of the current user stack
 *
 * Takes no arguments: the routine links an 8-byte frame and never reads
 * (0x8,A6).  It calls PROC1_$GET_USP and returns *(uint32_t *)usp, which for
 * a process stopped in the kernel is its user-mode return PC.  Nothing
 * validates the USP.
 *
 * Last entry of the FIM_ module in the SR10.4 map order; see
 * fim/get_user_pc.c.
 *
 * Address: 0x00e0aaa6 (22 bytes)
 */
uint32_t FIM_$GET_USER_PC(void);

/*
 * FIM_$BUILD_DF - Build a delivery frame for fault delivery
 *
 * This is the main fault handling function that:
 * 1. Extracts fault information from exception frame
 * 2. Checks if user FIM handler exists
 * 3. Saves FPU state if needed
 * 4. Builds delivery frame on user stack
 * 5. Sets up return to user FIM handler
 *
 * Parameters:
 *   exception_frame - Pointer to m68k exception frame
 *   return_pc - PC to use for return
 *   regs - Saved register set
 *   flags - Fault flags
 *   signal_params - Signal parameters
 *   status - Fault status code
 *   result - Output: delivery frame pointers
 *
 * Returns:
 *   0xFF if fault delivered to user, 0 if handled locally
 *
 * Address: 0x00E0A458 (1296 bytes) -- first entry of the FIM_ code segment
 * in the SAU2 link map.
 */
uint8_t FIM_$BUILD_DF(void *exception_frame, uint32_t return_pc,
                      fim_regs_t *regs, uint16_t flags,
                      uint16_t signal_param, uint32_t status,
                      void **result);

/*
 * ============================================================================
 * Function Prototypes - Assembly Functions
 * ============================================================================
 */

/*
 * FIM_$EXIT - Return from exception
 *
 * The rte that handlers jump to; patched to nop while a trace fault is
 * pending, when it falls into its trace arm (fim/sau2/exit.s).
 * Address: 0x00E228BC (72 bytes with the trace arm)
 */
void FIM_$EXIT(void);

/*
 * FIM_$UII - Unimplemented Instruction Interrupt handler
 *
 * Handles illegal/unimplemented instruction traps.
 * Address: 0x00E2146C (48 bytes with its descriptor words, fim/sau2/uii.s)
 * -- SAU2 link map and Ghidra agree; this
 * file previously carried 0x00e21326, which is not an entry point at all.
 */
void FIM_$UII(void);

/*
 * FIM_$GENERATE - Raise the fault `status' at the caller's return point
 *
 * Pops its return address and the status and enters FIM_$SOFT_FAULT's
 * descriptor tail (0x00E21458).  No C caller.
 * Address: 0x00E214A8 (6 bytes, fim/sau2/generate.s)
 */
void FIM_$GENERATE(status_$t status);

/*
 * FIM_$PRIV_VIOL - Privilege violation handler
 *
 * Handles privilege violation exceptions (user mode trying
 * to execute supervisor-only instructions).
 * Address: 0x00E21530 (74 bytes) -- SAU2 link map and Ghidra agree; this
 * file previously carried 0x00e212d8, which lies inside FIM_$USER_FIM_ADDR.
 */
void FIM_$PRIV_VIOL(void);

/*
 * FIM_$FLINE - F-Line (coprocessor) exception handler
 *
 * Handles F-Line traps for lazy FPU context switching.
 * When a process executes an FPU instruction without owning the FPU,
 * saves the previous owner's FP state, restores the new owner's,
 * and retries the instruction. If no FPU is present or the current
 * process already owns the FPU, forwards to FIM_$UII.
 *
 * Address: 0x00e21acc (68 bytes)
 */
void FIM_$FLINE(void);

/*
 * FIM_$ILLEGAL_USP - Illegal USP handler
 *
 * Handles invalid user stack pointer situations.
 * Address: 0x00E2158A (12 bytes with its descriptor words) -- see
 * fim/sau2/illegal_usp.s.  (This file used to
 * give 0x00E216D2, which is inside the FIM_$CLEANUP_STACK zero fill.)
 */
void FIM_$ILLEGAL_USP(void);

/*
 * FIM_$CLEANUP - Set up cleanup handler
 *
 * Establishes a cleanup handler for the current process.
 * Similar to setjmp - returns status_$cleanup_handler_set initially,
 * then a different status when cleanup is triggered.
 *
 * Parameters:
 *   handler - Cleanup handler context (receives jmp_buf-like data)
 *
 * Returns:
 *   status_$cleanup_handler_set on initial call
 *   Error status when cleanup triggered
 *
 * Address: 0x00e21634 (40 bytes)
 */
status_$t FIM_$CLEANUP(void *handler);

/*
 * FIM_$RLS_CLEANUP - Release cleanup handler
 *
 * Removes the most recently established cleanup handler.
 * Restores the cleanup context from the provided buffer.
 *
 * Parameters:
 *   cleanup_data - Pointer to cleanup context buffer
 *
 * Address: 0x00e2165c (22 bytes)
 */
void FIM_$RLS_CLEANUP(void *cleanup_data);

/*
 * FIM_$POP_SIGNAL - Pop signal from handler stack
 *
 * Restores the stack pointer from the cleanup context and
 * returns to the caller.
 *
 * Parameters:
 *   cleanup_data - Pointer to cleanup context buffer
 *
 * Address: 0x00e21672 (12 bytes)
 */
void FIM_$POP_SIGNAL(void *cleanup_data);

/*
 * FIM_$SIGNAL_FIRST - Signal first handler
 *
 * Invokes cleanup handler without unwinding.
 *
 * Parameters:
 *   status - Status to deliver
 *
 * Address: 0x00e2167e (10 bytes)
 */
void FIM_$SIGNAL_FIRST(status_$t status);

/*
 * FIM_$SIGNAL - Signal cleanup handlers
 *
 * Invokes the cleanup handler chain, similar to longjmp.
 * Does not return if a handler is established.
 *
 * Parameters:
 *   status - Status to deliver to handler
 *
 * Address: 0x00e21688 (42 bytes)
 */
void FIM_$SIGNAL(status_$t status);

/*
 * FIM_$PROC2_STARTUP - Process 2 startup entry point
 *
 * Entry point for new process startup after fork.
 *
 * @param context   Startup context containing stack pointer and entry point
 *
 * Address: 0x00e217b6 (30 bytes)
 */
void FIM_$PROC2_STARTUP(void *context);

/*
 * FIM_$SINGLE_STEP - Single step exception handler
 *
 * Handles trace exceptions for single-step debugging.
 * Address: 0x00E217D4 (80 bytes) -- see fim/sau2/single_step.s.  (This file used to
 * give 0x00E21754, which is inside the FIM_$CLEANUP_STACK zero fill.)
 */
void FIM_$SINGLE_STEP(void);

/*
 * FIM_$FAULT_RETURN - Return from fault handler
 *
 * Restores state and returns from user fault handler.
 * Optionally restores FP state, then rebuilds an exception frame
 * from the sigcontext and returns to user mode via RTE.
 *
 * Parameters:
 *   context_ptr  - Pointer to pointer to sigcontext_t
 *   regs_ptr     - Pointer to pointer to register save area (or NULL)
 *   fp_state_ptr - Pointer to FP state (or NULL)
 *
 * Does not return.
 *
 * Address: 0x00e21828 (80 bytes)
 */
NORETURN void FIM_$FAULT_RETURN(sigcontext_t **context_ptr,
                                uint32_t **regs_ptr,
                                void *fp_state_ptr);

/*
 * FIM_$FP_ABORT - Floating point abort handler
 *
 * Handles floating point exception aborts.
 * Address: 0x00E21B80 (48 bytes, fim/sau2/fp_abort.s)
 */
void FIM_$FP_ABORT(void);

/*
 * FIM_$FP_INIT - Initialize floating point state
 *
 * Parameters:
 *   asid - address space id for the process
 *
 * Initializes the 68881/68882 FPU for a process.
 * Address: 0x00E21BB0 (84 bytes and the 48-byte fim_$copy_fp_frame,
 * fim/sau2/fp_init.s)
 */
void FIM_$FP_INIT(int16_t asid);

/*
 * FIM_$FSAVE - Save floating point state
 *
 * Saves 68881/68882 state using FSAVE instruction.
 *
 * Parameters:
 *   status - Output: 0 if no FP context, non-zero if saved
 *   sp_ptr - Pointer to stack pointer (modified)
 *   type - FP save type
 *   unused - Unused parameter
 *
 * Address: 0x00E21C34 (160 bytes, fim/sau2/fsave.s)
 */
void FIM_$FSAVE(int16_t *status, uint32_t *sp_ptr, uint16_t type, uint8_t unused);

/*
 * FIM_$FRESTORE - Restore floating point state
 *
 * Restores 68881/68882 state using FRESTORE instruction.
 *
 * Parameters:
 *   state_ptr - Pointer to saved FP state
 *
 * Address: 0x00E21CD4 (116 bytes, fim/sau2/frestore.s)
 */
void FIM_$FRESTORE(void *state_ptr);

/*
 * FIM_$FP_GET_STATE - Get floating point state
 *
 * Gets the complete 68881/68882 state including registers.
 *
 * Parameters:
 *   state - Output buffer for FP state
 *   status - Status return
 *
 * Address: 0x00E21DC2 (196 bytes) -- SAU2 link map and Ghidra agree; this
 * file previously carried 0x00e21d48, which the map names FP_$GET_FP.
 */
void FIM_$FP_GET_STATE(void *state, status_$t *status);

/*
 * FIM_$FP_PUT_STATE - Put floating point state
 *
 * Sets the complete 68881/68882 state including registers.
 *
 * Parameters:
 *   state - FP state to restore
 *   status - Status return
 *
 * Address: 0x00E21E86 (152 bytes) -- SAU2 link map and Ghidra agree; this
 * file previously carried 0x00e21e0c, which is inside FIM_$FP_GET_STATE.
 */
void FIM_$FP_PUT_STATE(void *state, status_$t *status);

/*
 * FIM_$SPURIOUS_INT - Spurious interrupt handler
 *
 * Handles spurious interrupts (no device acknowledged).
 * Address: 0x00E21F20 (86 bytes) -- SAU2 link map and Ghidra agree; this
 * file previously carried 0x00e21ea4, which is inside FIM_$FP_PUT_STATE.
 * The spurious-interrupt counter FIM_$SPUR_CNT follows at 0x00E21F7E.
 */
void FIM_$SPURIOUS_INT(void);

/*
 * FIM_$PARITY_TRAP - Parity error trap handler
 *
 * Handles memory parity errors.
 * Address: 0x00E21F84 (98 bytes) -- SAU2 link map and Ghidra agree; this
 * file previously carried 0x00e21efa, which is inside FIM_$SPURIOUS_INT.
 * PARITY_$INFO follows at 0x00E21FE6.
 */
void FIM_$PARITY_TRAP(void);

/*
 * FIM_$GET_USER_SR_PTR - Get pointer to user SR in exception frame
 *
 * Returns pointer to the SR word in an exception frame for a process.
 *
 * Parameters:
 *   process - Process ID
 *   unused - Unused parameters (D2 value)
 *
 * Returns:
 *   Pointer to SR word
 *
 * Address: 0x00E2277C (118 bytes and a 116-byte frame checker,
 * fim/sau2/get_user_sr_ptr.s)
 */
void *FIM_$GET_USER_SR_PTR(uint16_t process, uint32_t unused);

/*
 * FIM_$DELIVER_TRACE_FAULT - Deliver trace fault to process
 *
 * Marks the specified address space for receiving a trace fault.
 * Sets the trace bit and increments the pending trace faults counter.
 *
 * Parameters:
 *   as_id - Address space ID to deliver trace fault to
 *
 * Address: 0x00E22866 (42 bytes, fim/sau2/deliver_trace_fault.s)
 */
void FIM_$DELIVER_TRACE_FAULT(int16_t as_id);

/*
 * FIM_$CLEAR_TRACE_FAULT - Clear trace fault state
 *
 * Address: 0x00E22890 (44 bytes) -- see fim/sau2/clear_trace_fault.s
 */
void FIM_$CLEAR_TRACE_FAULT(int16_t as_id);   /* move.w (0x4,SP),D0: one word argument */

/*
 * FIM_$CRASH - System crash handler
 *
 * Called when a fault cannot be delivered or is fatal.
 * Prints the fault report (frame pointer, SR, PC, format word, class and,
 * for bus/address errors, fault address and SSW), records the saved A7 in
 * CRASH_REPORT (the map's CRASH_SP), reloads the saved registers and calls
 * CRASH_SYSTEM(status); returns if CRASH_SYSTEM does.
 *
 * Parameters:
 *   exception_frame - Pointer to exception frame
 *   regs - Saved register set (D0..A7)
 *   status - handed to CRASH_SYSTEM (read at 0xE(SP) after the SR push;
 *            FIM_$SPURIOUS_INT passes all three)
 *
 * Address: 0x00E1E864 (158 bytes of code and a 124-byte report template,
 * fim/sau2/crash.s)
 */
void FIM_$CRASH(void *exception_frame, fim_regs_t *regs, status_$t *status);

/*
 * FIM_$BUS_ERR - Bus error trap handler (assembly entry point)
 *
 * OS_$INIT installs this as the PROM bus-error trap vector
 * (_BUS_ERROR_VEC = &FIM_$BUS_ERR) once initialization is
 * far enough along to handle bus errors itself; the vector table cell
 * that names it is at 0x00E342E8.
 *
 * Transcribed in fim/sau2/bus_err.s.
 *
 * Address: 0x00E218E8 (484 bytes)
 */
extern void FIM_$BUS_ERR(void);

/*
 * BUS_ERROR_SWITCH - installable bus-timeout recovery handler
 *
 * This cell is literally the absolute operand of the "jmp" instruction at
 * JMP_TO_BUS_ERR (0x00E218CA).  When FIM_$BUS_ERR classifies a fault as a
 * bus timeout it checks this cell; if it is non-zero it clears the cache,
 * restores the registers it saved and branches to JMP_TO_BUS_ERR, which
 * jumps here.  The target inherits the supervisor stack with the exception
 * frame still on it and is responsible for unwinding it.
 *
 * io_$probe (0x00E29138) arms it around a hardware probe so that touching
 * an absent controller reports "not present" instead of crashing.  Zero
 * means nobody is fielding bus timeouts, and the handler delivers a SIGBUS
 * fault instead.
 *
 * Address: 0x00E218CC (defined in fim/sau2/bus_err.s)
 */
extern void *BUS_ERROR_SWITCH;

/*
 * JMP_TO_BUS_ERR - the "jmp (BUS_ERROR_SWITCH).l" trampoline itself
 *
 * Address: 0x00E218CA (defined in fim/sau2/bus_err.s)
 */
extern void JMP_TO_BUS_ERR(void);

/*
 * FIM_$CLEANUP_STACK - cleanup handler stack heads, one longword per process
 *
 * Each entry is the head of that process's cleanup-handler chain (a
 * fim_cleanup_entry_t built on the process's own stack by FIM_$CLEANUP), or
 * NULL when no handler is established.  The original performs no bounds
 * check on the index.
 *
 * Element size and index: every instruction that reaches this table loads
 * PROC1_$CURRENT (the word at 0x00E20608), scales it by four with "lsl.w #2"
 * and adds it to the table base as a signed word index, then moves a
 * longword.  All three sites are in the cleanup/signal group and all three
 * materialise the same base PC-relatively:
 *
 *   FIM_$CLEANUP     0x00E21640  lea (0x70,PC),A0   ; 0x00E21642 + 0x70
 *   FIM_$RLS_CLEANUP 0x00E21668  lea (0x48,PC),A0   ; 0x00E2166A + 0x48
 *   FIM_$SIGNAL      0x00E21692  lea (0x1e,PC),A0   ; 0x00E21694 + 0x1e
 *
 * All three resolve to 0x00E216B2, so the element size is 4.
 *
 * Element count: the object runs from 0x00E216B2 to the next object in the
 * image, FIM_$PROC2_STARTUP at 0x00E217B6 (everything in between reads back
 * as zero with "gsk read 0x00E216B2 260").  That is 0x104 = 260 bytes = 65
 * longwords, which is exactly PROC1_MAX_PROCESSES -- the same count as
 * PCBS[], PROC1_$DATA.type[] and OS_STACK_BASE[], the other tables indexed by a
 * PROC1 process id.
 *
 * Address: 0x00E216B2.  The SAU2 link map exports no symbol here (the table
 * is module-local in the original), so this is a tree name.  Because the
 * image places it inside the FIM_ code region between FIM_$SIGNAL and
 * FIM_$PROC2_STARTUP, it is defined -- as 260 zero bytes, which is what the
 * image holds -- with FIM_$SIGNAL in fim/sau2/signal.s, and not in
 * fim/fim_data.c.
 */
extern void *FIM_$CLEANUP_STACK[PROC1_MAX_PROCESSES];

/*
 * FIM_$TRACE_BIT - per-address-space pending trace fault bit, 1 byte per AS
 *
 * Bit 7 of FIM_$TRACE_BIT[as] is the "a trace fault is pending for this
 * address space" flag.  FIM_$DELIVER_TRACE_FAULT (0x00E22866) sets it with
 * "bset.b #7", FIM_$CLEAR_TRACE_FAULT (0x00E22890) clears it with
 * "bclr.b #7", and each transition that changes the bit also steps
 * FIM_$PENDING_TRACE_FAULTS (0x00E21FFE) and, at the zero boundary, patches
 * the instruction at FIM_$EXIT (0x00E228BC) between RTE and NOP.
 *
 * Address: 0x00E21890, stride 1, FIM_AS_COUNT elements; it is the first
 * object of the module's wired data area, so it is defined -- as 58 zero
 * bytes, which is what the image holds -- in fim/sau2/trace_bit.s, linked
 * after fim/sau2/fault_return.s (FIM_$SETUP_RETURN), not in fim/fim_data.c.
 * 0x00E21890 + 58 = 0x00E218CA, the address of JMP_TO_BUS_ERR.
 */
extern uint8_t FIM_$TRACE_BIT[];

/*
 * Fault descriptor built on the supervisor stack and handed to FIM_$COM
 * (0x00E213A4) as its second argument.  FIM_$BUS_ERR and FIM_$PARITY_TRAP
 * (0x00E21F84) both build this triple by hand.
 *
 * FIM_$COM is entered - by jmp, not call - with
 *   (0x00,SP) = pointer to the CPU exception frame
 *   (0x04,SP) = pointer to one of these
 */
typedef struct fim_fault_desc_t {
    status_$t   status;         /* 0x00: status_$t to report */
    uint16_t    signal;         /* 0x04: BSD signal number (SIGBUS=10, SIGSEGV=11) */
    uint16_t    fault_class;    /* 0x06: 0x3000 for memory access faults */
} __attribute__((packed)) fim_fault_desc_t;

/* Remaining documented offsets (bead source-pewa). */
_Static_assert(__builtin_offsetof(fim_fault_desc_t, status) == 0x00, "fim_fault_desc_t.status");
_Static_assert(__builtin_offsetof(fim_fault_desc_t, signal) == 0x04, "fim_fault_desc_t.signal");
_Static_assert(__builtin_offsetof(fim_fault_desc_t, fault_class) == 0x06, "fim_fault_desc_t.fault_class");

#if defined(ARCH_M68K)
_Static_assert(sizeof(fim_fault_desc_t) == 8, "fim_fault_desc_t must be 8 bytes");
#endif

/* fault_class values seen in the image */
#define FIM_FAULT_CLASS_ACCESS  0x3000  /* Memory access fault (bus error, parity) */
#define FIM_FAULT_CLASS_OTHER   0xA000  /* Everything else in the FIM_$COM table */

/*
 * status_$t values FIM_$BUS_ERR reports.  The names follow the SR10.4
 * status-code database (stcodes/stcode.db.10.4), which gives the message
 * text quoted beside each code; module 0x12 is the fault module and module
 * 0x07 is the MMU.
 */
#define status_$fault_access_violation      0x00120011  /* "access violation" */
/* "cleanup handler set" (SR10.4 stcodes 120035): the value PFM_$CLEANUP
 * returns on its initial (non-fault) return.  Single definition; acl/, ec/
 * and proc2/ used to carry copies. */
#define status_$cleanup_handler_set         0x00120035
#define status_$fault_bus_time_out          0x0012000C  /* "bus time-out" */
#define status_$fault_process_quit          0x00120010  /* "process quit" */

#endif /* FIM_H */
