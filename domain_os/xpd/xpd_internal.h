/*
 * xpd/xpd_internal.h - XPD internal definitions
 *
 * The PROC2 entry fields, the debug-state record CAPTURE_FAULT parks in its
 * frame, and the constant cells of the XPD code segments.
 */

#ifndef XPD_INTERNAL_H
#define XPD_INTERNAL_H

#include "xpd/xpd.h"
#include "proc2/proc2_internal.h"   /* PROC2_$EC / PROC_CR_REC_EC - TODO: no
                                     * public declaration exists yet (bead
                                     * source-xpd-ec) */
#include "fim/fim.h"
#include "fp/fp.h"
#include "os/os.h"
#include "peb/peb.h"
#include "acl/acl.h"
#include "uid/uid.h"                /* UID_$NIL */

/*
 * ============================================================================
 * The PROC2 process table as XPD addresses it
 * ============================================================================
 *
 * A_n = 0xEA551C + index*0xE4 is entry_base + 0xE4, so (-d,An) = +0xE4-d:
 *   (-0xC8) +0x1C self_index      (-0x50) +0x94 pad_94: event code << 8 |
 *   (-0xBE) +0x26 debugger_idx                          signal (word)
 *   (-0xBA) +0x2A flags, high byte (-0x4E) +0x96 asid
 *   (-0xB9) +0x2B flags, low byte  (-0x4A) +0x9A level1_pid
 *   (-0x70) +0x74 sig_blocked_1    (-0x22) +0xC2 fault_param (BE longword)
 *   (-0x6C) +0x78 sig_blocked_2    (-0x1E) +0xC6 debug-state record VA
 *   (-0x64) +0x80 sig_mask_2       (-0x1A) +0xCA last stopped PC
 *                                  (-0x16) +0xCE ptrace_opts (14 bytes)
 */
#define XPD_ENTRY(idx)        P2_INFO_ENTRY(idx)
#define XPD_CURRENT_INDEX()   P2_PID_TO_INDEX(PROC1_$CURRENT)
#define XPD_PTRACE_OPTS(e)    ((xpd_$ptrace_opts_t *)(e)->ptrace_opts)

/* The word at +0x2A, touched by byte ops on its LOW byte (+0x2B) */
#define XPD_PF_TRACE_PENDING  0x0002    /* bit 1: a trace fault is armed */
#define XPD_PF_SUSPENDED      0x0010    /* bit 4: stopped for the debugger */
#define XPD_PF_STATE_SAVED    0x0020    /* bit 5: registers were handed out */
#define XPD_PF_DEBUG_TARGET   0x0080    /* bit 7 (`tst.b` / `bmi`) */

/* +0xC6 and +0xCA are the two longwords inside proc2_info_t.pad_c6,
 * big-endian in the image; read and written with shifts. */
#define XPD_ENTRY_BE32_GET(p) \
    (((uint32_t)(p)[0] << 24) | ((uint32_t)(p)[1] << 16) | \
     ((uint32_t)(p)[2] << 8) | (uint32_t)(p)[3])
#define XPD_ENTRY_BE32_SET(p, v) do { \
    (p)[0] = (uint8_t)((uint32_t)(v) >> 24); (p)[1] = (uint8_t)((uint32_t)(v) >> 16); \
    (p)[2] = (uint8_t)((uint32_t)(v) >> 8);  (p)[3] = (uint8_t)(uint32_t)(v); \
} while (0)
#define XPD_ENTRY_STATE_VA(e)        XPD_ENTRY_BE32_GET(&(e)->pad_c6[0])
#define XPD_ENTRY_STATE_VA_SET(e, v) XPD_ENTRY_BE32_SET(&(e)->pad_c6[0], (v))
#define XPD_ENTRY_LAST_PC(e)         XPD_ENTRY_BE32_GET(&(e)->pad_c6[4])
#define XPD_ENTRY_LAST_PC_SET(e, v)  XPD_ENTRY_BE32_SET(&(e)->pad_c6[4], (v))

/*
 * ============================================================================
 * The debug-state record (XPD_$CAPTURE_FAULT's frame, A6-0x18..A6-0x07)
 * ============================================================================
 *
 * Its address goes to entry+0xC6 while the target is stopped; GET/PUT
 * _REGISTERS read the five fields at +0, +4, +8, +0xC, +0x10 of it.  The
 * pointers are the native kind because the record only ever lives in a
 * stack frame of the running kernel.
 */
typedef struct xpd_$frame_t {
    uint16_t sr;                /* +0 */
    uint8_t pc[4];              /* +2: big-endian, unaligned */
} __attribute__((packed)) xpd_$frame_t;

#define XPD_FRAME_PC(f)        XPD_ENTRY_BE32_GET((f)->pc)
#define XPD_FRAME_PC_SET(f, v) XPD_ENTRY_BE32_SET((f)->pc, (v))

typedef struct xpd_$debug_state_t {
    xpd_$frame_t *frame;        /* +0x00: the fault's SR/PC frame */
    uint32_t *regs;             /* +0x04: D0-D7 / A0-A7 */
    uint32_t *fp_buf;           /* +0x08: {length, FP state...}, 0x100 bytes */
    uint32_t *aux;              /* +0x0C: {length, words...}, 0xF0 bytes */
    int8_t fp_modified;         /* +0x10: PUT_REGISTERS wrote something */
} xpd_$debug_state_t;

#if defined(ARCH_M68K)
_Static_assert(__builtin_offsetof(xpd_$debug_state_t, regs) == 0x04, "xpd_$debug_state_t.regs");
_Static_assert(__builtin_offsetof(xpd_$debug_state_t, fp_buf) == 0x08, "xpd_$debug_state_t.fp_buf");
_Static_assert(__builtin_offsetof(xpd_$debug_state_t, aux) == 0x0C, "xpd_$debug_state_t.aux");
_Static_assert(__builtin_offsetof(xpd_$debug_state_t, fp_modified) == 0x10, "xpd_$debug_state_t.fp_modified");
#endif

#define XPD_FP_BUF_BYTES   0x100    /* A6-0x118..A6-0x18 */
#define XPD_AUX_BUF_BYTES  0x0F0    /* A6-0x208..A6-0x118 */

/*
 * ============================================================================
 * Constant cells (xpd_data.c)
 * ============================================================================
 */
extern uint16_t xpd_$wire_limit;            /* 0x00E3238A: 3 */
extern void *PTR_XPD_$DATA;                 /* 0x00E32390: 0x00EA5034 */
extern int8_t xpd_$find_asid_flag;          /* 0x00E5BDBE: 0 */
extern xpd_$response_t xpd_$response_two;   /* 0x00E5BDC0: 2 */
extern xpd_$response_t xpd_$unreg_response; /* 0x00E75044: 2 */
extern xpd_$event_type_t xpd_$cleanup_event;/* 0x00E7508A: 3 */
extern status_$t xpd_$cleanup_status;       /* 0x00E7508C: 0 */

/*
 * FPU configuration flags (peb/peb.h, both Domain booleans):
 *   0x00E24C98 M68881_$SAVE_FLAG   - MC68881/68882 present
 *   0x00E24C92 PEB_$INSTALLED_FLAG - peripheral-board FPU present
 */

#endif /* XPD_INTERNAL_H */
