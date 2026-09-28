/*
 * XPD - eXtended Process Debugging
 *
 * A debugger process registers itself in a six-slot table, is linked to its
 * targets through proc2_info_t.debugger_idx, and is told about a target's
 * faults / fork / exec / exit through a per-target record in XPD_$DATA whose
 * state word carries the debugger slot, the event code, the debugger's
 * response and two flags.  The target suspends itself (PROC1_$SUSPEND or an
 * EC_$WAIT on its own eventcount) until the debugger continues it.
 *
 * Map:
 *   I E32304 XPD size = 90        XPD_$INIT
 *   I E5AF38 XPD size = 126C      the entry points, XPD_$FIND_INDEX first
 *   I E5C1A4 XPD_KER size = 420   register / FP access
 *   I E74F7C XPD size = 1BC       UNREGISTER_DEBUGGER, CLEANUP, POST_EVENT
 *   D EA5034 XPD_$DATA size = 4E8 the target / debugger records
 *   D E35148 XPD size = 4, D E81810 XPD size = 4: two module data cells
 *     whose base+4 the routines load into A5 and never use.
 *
 * All routines reach the PROC2 process table as 0xEA551C + index*0xE4, i.e.
 * one entry PAST the indexed entry, so a displacement (-d,An) is entry
 * offset 0xE4 - d (proc2/proc2.h has the field map).
 */

#ifndef XPD_H
#define XPD_H

#include "base/base.h"
#include "ec/ec.h"
#include "ml/ml.h"
#include "proc1/proc1.h"
#include "mst/mst.h"     /* status_$mst_guard_fault (0x0004000a) */
#include "proc2/proc2.h"

/* The ML resource the debugger table is guarded by (`move.w #0x2` before
 * ML_$LOCK throughout). */
#define XPD_LOCK_ID 2

/*
 * Event type word passed by reference to XPD_$POST_EVENT: its LOW byte
 * (0x00E750D2 `move.b (0x1,A1)`) is the event code stored in the state
 * word's bits 5-8.
 */
typedef uint16_t xpd_$event_type_t;

/*
 * Debugger response word.  XPD_$POST_EVENT returns bits 12-13 of the state
 * word in it (2 = no debugger); XPD_$CONTINUE_PROC takes its LOW byte
 * (0x00E5BF2E `move.b (0x1,A1)`) and stores it there.
 */
typedef uint16_t xpd_$response_t;

/*
 * Status codes (stcode.db.10.2, modules 0x12 fault, 0x16 xpd, 0x19 proc2)
 */
#define status_$xpd_invalid_state_argument            0x00160003
#define status_$xpd_not_a_debugger                    0x00160005
#define status_$xpd_debugger_not_found                0x00160006
#define status_$xpd_debugger_table_full               0x00160007
#define status_$xpd_already_a_debugger                0x00160009
#define status_$xpd_target_not_suspended              0x0016000B
#define status_$xpd_invalid_ec_key                    0x0016000C
#define status_$xpd_state_unavailable_for_this_event  0x0016000E
#define status_$xpd_invalid_option                    0x0016000F
#define status_$xpd_illegal_target_setup              0x00160011
#define status_$xpd_target_is_forking                 0x00160012
#define status_$xpd_target_is_execing                 0x00160013
#define status_$xpd_target_is_invoking                0x00160014
#define status_$xpd_target_is_exiting                 0x00160015
#define status_$xpd_target_is_loading_exec_image      0x00160016
#define status_$xpd_target_is_vforking                0x00160017
#define status_$xpd_target_is_signalled               0x00160019
#define status_$fault_single_step_completed           0x00120015
#define status_$fault_process_BLAST                   0x00120019
#define status_$fault_cleanup_in_progress             0x00120035

/*
 * Ptrace options record, 14 bytes, kept at proc2_info_t +0xCE
 * (XPD_$SET_PTRACE_OPTS 0x00E5B044 `lea (-0x16,A0),A2`).
 */
typedef struct xpd_$ptrace_opts_t {
  uint32_t signal_mask;    /* 0x00: bit n-1 set = event/signal n is traced
                            *       (XPD_$CAPTURE_FAULT 0x00E5B3BE) */
  uint32_t trace_range_lo; /* 0x04: PC range for single-step tracing */
  uint32_t trace_range_hi; /* 0x08 */
  uint8_t flags;           /* 0x0C: bit n = capture event code n
                            *       (0x00E5B29E `btst.l D1,D0` with the
                            *       event code in D1): 1 fork/vfork, 2
                            *       exec/invoke, 4 exit, 5 load-image /
                            *       signalled; bit 0 = capture faults
                            *       (0x00E5B3A4); bit 6 = trace the PC
                            *       range, bit 7 = trace outside it
                            *       (0x00E5B344 / 0x00E5B362) */
  uint8_t flags2;          /* 0x0D: bit 3 = inherit on fork
                            *       (XPD_$INHERIT_PTRACE_OPTIONS); bits 2
                            *       and 7 = exec/invoke are reported as
                            *       signal 5 once (xpd_$exec_event) */
} __attribute__((packed, aligned(2))) xpd_$ptrace_opts_t;

_Static_assert(sizeof(xpd_$ptrace_opts_t) == 14, "xpd_$ptrace_opts_t is 14 bytes");

/*
 * ============================================================================
 * XPD_$DATA (0x00EA5034, 0x4E8 bytes)
 * ============================================================================
 *
 * +0x000  target records, 0x14 bytes each, indexed by the PROC2 table index
 *         (1..57: XPD_$UNREGISTER_DEBUGGER walks 57 from +0x14).  Index 57
 *         sits at +0x474 and overlaps the unused debugger slot 0.
 * +0x478  debugger records, 0x10 bytes each, indexed 1..6 (slot 0 unused:
 *         XPD_$FIND_DEBUGGER_INDEX starts at +0x488, asid at +0x494).
 *
 * The records are addressed through the byte array so the overlap is
 * reproduced exactly; XPD_$INIT's 58 target EC_$INITs (0x00E32348
 * `moveq #0x39`) run into the debugger area before the 6 debugger ECs are
 * initialised over them.
 */
#define XPD_MAX_DEBUGGERS       6
#define XPD_MAX_TARGETS         57

typedef struct xpd_$target_t {
  ec_$eventcount_t ec;      /* 0x00: the target waits here for its debugger */
  status_$t status;         /* 0x0C: the event's status (XPD_$POST_EVENT) */
  uint16_t state;           /* 0x10: XPD_STATE_* */
  uint16_t pad_12;          /* 0x12 */
} xpd_$target_t;

typedef struct xpd_$debugger_t {
  ec_$eventcount_t ec;      /* 0x00: advanced when a target posts an event */
  uint16_t asid;            /* 0x0C: the debugger's address space, 0 = free */
  uint16_t pad_0e;          /* 0x0E */
} xpd_$debugger_t;

/*
 * The strides and the debugger-table offset are derived from the record
 * sizes so that a host build - where ec_$eventcount_t carries two native
 * pointers - keeps the same shape; the m68k asserts pin the image values.
 */
#define XPD_TARGET_RECORD_SIZE   sizeof(xpd_$target_t)
#define XPD_DEBUGGER_RECORD_SIZE sizeof(xpd_$debugger_t)
#define XPD_DEBUGGER_TABLE_OFF   (XPD_MAX_TARGETS * XPD_TARGET_RECORD_SIZE + 4)
#define XPD_DATA_SIZE            (XPD_DEBUGGER_TABLE_OFF + (XPD_MAX_DEBUGGERS + 1) * XPD_DEBUGGER_RECORD_SIZE)

#if defined(ARCH_M68K)
_Static_assert(__builtin_offsetof(xpd_$target_t, status) == 0x0C, "xpd_$target_t.status");
_Static_assert(__builtin_offsetof(xpd_$target_t, state) == 0x10, "xpd_$target_t.state");
_Static_assert(__builtin_offsetof(xpd_$debugger_t, asid) == 0x0C, "xpd_$debugger_t.asid");
_Static_assert(XPD_TARGET_RECORD_SIZE == 0x14, "xpd_$target_t size");
_Static_assert(XPD_DEBUGGER_RECORD_SIZE == 0x10, "xpd_$debugger_t size");
_Static_assert(XPD_DEBUGGER_TABLE_OFF == 0x478, "debugger table at +0x478");
_Static_assert(XPD_DATA_SIZE == 0x4E8, "XPD_$DATA: map size 4E8");
#endif

/*
 * The state word.  The image touches it both as a word and through its
 * HIGH byte (`(0x10,A2)` byte ops), so the byte bits are shown as word bits:
 */
#define XPD_STATE_ENABLED   0x8000  /* high-byte bit 7: events are captured
                                     * (XPD_$SET_ENABLE 0x00E5BFA2-0x00E5BFAE) */
#define XPD_STATE_ACKED     0x4000  /* high-byte bit 6: the debugger has read
                                     * the event (GET_EVENT_AND_DATA sets,
                                     * POST_EVENT clears) */
#define XPD_STATE_RESPONSE  0x3000  /* high-byte bits 4-5: the debugger's
                                     * response (CONTINUE_PROC writes) */
#define XPD_STATE_RESPONSE_SHIFT 12
#define XPD_STATE_DEBUGGER  0x0E00  /* high-byte bits 1-3: debugger slot */
#define XPD_STATE_DEBUGGER_SHIFT 9
#define XPD_STATE_EVENT     0x01E0  /* bits 5-8: the pending event code */
#define XPD_STATE_EVENT_SHIFT 5

#if defined(ARCH_M68K)
#define XPD_$DATA ((uint8_t *)0x00EA5034)
#else
extern uint8_t XPD_$DATA[XPD_DATA_SIZE];
#endif

#define XPD_TARGET(idx) \
    ((xpd_$target_t *)(XPD_$DATA + (uint32_t)(uint16_t)(idx) * XPD_TARGET_RECORD_SIZE))
#define XPD_DEBUGGER(slot) \
    ((xpd_$debugger_t *)(XPD_$DATA + XPD_DEBUGGER_TABLE_OFF + \
                         (uint32_t)(uint16_t)(slot) * XPD_DEBUGGER_RECORD_SIZE))

/* Restart modes for XPD_$RESTART (the `jmp (PC,D0)` table at 0x00E5B614) */
#define XPD_RESTART_MODE_CONTINUE 1
#define XPD_RESTART_MODE_STEP 2
#define XPD_RESTART_MODE_STEP_NO_TRACE 3

/* Register-set selectors for XPD_$GET_REGISTERS / XPD_$PUT_REGISTERS */
#define XPD_REG_MODE_GENERAL 0
#define XPD_REG_MODE_EXCEPTION 1
#define XPD_REG_MODE_FP_STATE 2
#define XPD_REG_MODE_DEBUG_STATE 3

/*
 * ============================================================================
 * Entry points (addresses from the SAU2 map)
 * ============================================================================
 */

/* 0x00E32304 */
void XPD_$INIT(void);
/* 0x00E75046: called by the exit path for the current process */
void XPD_$CLEANUP(void);

/* 0x00E5BBD8 */
void XPD_$SET_DEBUGGER(uid_t *debugger_uid, uid_t *target_uid,
                       status_$t *status_ret);

/* 0x00E5AF9E / 0x00E5B076 / 0x00E5B156 / 0x00E5B174 */
void XPD_$SET_PTRACE_OPTS(uid_t *proc_uid, xpd_$ptrace_opts_t *opts,
                          status_$t *status_ret);
void XPD_$INQ_PTRACE_OPTS(uid_t *proc_uid, xpd_$ptrace_opts_t *opts,
                          status_$t *status_ret);
void XPD_$RESET_PTRACE_OPTS(xpd_$ptrace_opts_t *opts);
int8_t XPD_$INHERIT_PTRACE_OPTIONS(xpd_$ptrace_opts_t *opts);

/* 0x00E5B54A: mode 1..3, pc (1 = keep), signal, status (0 = keep) - all by
 * reference */
void XPD_$RESTART(uid_t *proc_uid, uint16_t *mode, int32_t *pc, int16_t *signal,
                  int32_t *status, status_$t *status_ret);
/* 0x00E5BED8 */
void XPD_$CONTINUE_PROC(uid_t *proc_uid, xpd_$response_t *response,
                        status_$t *status_ret);
/* 0x00E5BF50: *enable is a Domain boolean whose sign bit becomes
 * XPD_STATE_ENABLED */
void XPD_$SET_ENABLE(uid_t *proc_uid, int8_t *enable, status_$t *status_ret);

/* 0x00E5B1EE: `context` and `frame` are the ADDRESSES of the two pointer
 * cells PROC2_$DELIVER_FIM keeps in its frame (the register block and the
 * SR/PC frame); *signal and *status are in/out. */
void XPD_$CAPTURE_FAULT(void *context, int32_t *frame, uint16_t *signal,
                        status_$t *status_ret);
/* 0x00E75090 */
void XPD_$POST_EVENT(xpd_$event_type_t *event_type, status_$t *status_val,
                     xpd_$response_t *response_ret);
/* 0x00E5BE28 */
void XPD_$GET_EVENT_AND_DATA(uid_t *proc_uid, uint16_t *event_type,
                             status_$t *status_ret);
/* 0x00E5BDC2: *key must be 0; *ec_ret receives EC2_$REGISTER_EC1's result */
void XPD_$GET_EC(int16_t *key, void **ec_ret, status_$t *status_ret);

/* 0x00E5B954 / 0x00E5B88E / 0x00E5B9E2 / 0x00E5BA70 / 0x00E5BAA6.  `addr`
 * is the address in the target, passed by value; `len` by reference. */
void XPD_$READ_PROC(uid_t *proc_uid, void *addr, int32_t *len, void *buffer,
                    status_$t *status_ret);
void XPD_$READ_PROC_ASYNC(uid_t *proc_uid, void *addr, int32_t *len,
                          void *buffer, status_$t *status_ret);
void XPD_$WRITE_PROC(uid_t *proc_uid, void *addr, const int32_t *len,
                     const void *buffer, status_$t *status_ret);
void XPD_$READ(uint16_t *asid, void *addr, int32_t *len, void *buffer,
               status_$t *status_ret);
void XPD_$WRITE(uint16_t *asid, void *addr, const int32_t *len,
                const void *buffer, status_$t *status_ret);

/* 0x00E5C1A4 / 0x00E5C33C */
void XPD_$GET_REGISTERS(uid_t *proc_uid, int16_t *mode, void *regs,
                        status_$t *status_ret);
void XPD_$PUT_REGISTERS(uid_t *proc_uid, int16_t *mode, void *regs,
                        status_$t *status_ret);
/* 0x00E5BFFC / 0x00E5C094 */
void XPD_$GET_FP(uid_t *proc_uid, status_$t *status_ret);
void XPD_$PUT_FP(uid_t *proc_uid, status_$t *status_ret);
/* 0x00E5C12C */
void XPD_$GET_TARGET_INFO(uid_t *proc_uid, int8_t *is_target,
                          int8_t *is_suspended, status_$t *status_ret);

/*
 * ============================================================================
 * Module-internal routines that other subsystems' tests mock
 * ============================================================================
 */

/* 0x00E5AF38: PROC2 index of the target, checking the caller is its
 * debugger and it is suspended */
int16_t XPD_$FIND_INDEX(uid_t *proc_uid, status_$t *status_ret);
/* 0x00E5BADC / 0x00E5BB1E / 0x00E74F7C: debugger slot (1..6, 0 = none) */
int16_t XPD_$FIND_DEBUGGER_INDEX(int16_t asid, status_$t *status_ret);
int16_t XPD_$REGISTER_DEBUGGER(int16_t asid, status_$t *status_ret);
void XPD_$UNREGISTER_DEBUGGER(int16_t asid, status_$t *status_ret);
/* 0x00E5B704 */
void XPD_$COPY_MEMORY(int16_t dst_asid, void *dst_addr, int16_t src_asid,
                      const void *src_addr, uint32_t len,
                      status_$t *status_ret);
/* 0x00E5C50E / 0x00E5C4D0: `fp_buf` is the 0x100-byte FP save area and
 * `aux` the longword (plus what follows it) FIM_$FP_GET_STATE fills */
void XPD_$FP_GET_STATE(void *fp_buf, void *aux);
void XPD_$FP_PUT_STATE(void *fp_buf, void *aux);
/* 0x00E5C55A / 0x00E5C58E */
void XPD_$GET_FP_INT(int16_t *asid, status_$t *status_ret);
void XPD_$PUT_FP_INT(int16_t *asid, status_$t *status_ret);

#endif /* XPD_H */
