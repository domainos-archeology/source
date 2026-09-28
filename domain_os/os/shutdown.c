/*
 * OS_$SHUTDOWN - orderly system shutdown
 *
 * Original address: 0x00E6D476 (SAU2 map: OS_ code segment)
 * Size: 434 bytes (0x00E6D476 .. 0x00E6D627); the constant cells and
 * strings it uses follow at 0x00E6D628 .. 0x00E6D697.
 *
 * Only process 1 or a caller whose SIDs name the locksmith may shut the
 * system down.  The subsystems are then shut down in a fixed order, the
 * shutdown-wired code and data are wired, the volumes are dismounted
 * (twice), a message is shown, the network service mask is cleared, a
 * 2001-iteration multiply loop delays, and CRASH_SYSTEM is entered with
 * the volume shutdown status (0 for a clean shutdown).
 *
 * Frame (link.w A6,-0x264; A5 saved and set to 0xE82728, the OS data
 * segment: (0xc,A5) = OS_$SHUTTING_DOWN_FLAG, (0x10,A5) = the clock_t
 * OS_$SHUTDOWN_WAIT_TIME at 0xE82738):
 *   (0x8,A6)    status_p      pointer; *status_p is copied to (-0x258,A6)
 *                             and never read again
 *   (-0x68,A6)  line          the 100-byte VFMT buffer
 *   (-0x90,A6)  cur_sids      ACL_$GET_RE_SIDS's second output, compared
 *                             with RGYC_$G_LOCKSMITH_UID (0xE17434)
 *   (-0xb8,A6)  orig_sids     its first output
 *   (-0x248,A6) wire_pages    the MST_$WIRE_AREA page list (100 longwords)
 *   (-0x24c,A6) svc_value     the zero longword for NETWORK_$SET_SERVICE
 *   (-0x250,A6) volx_status   VOLX_$SHUTDOWN's result, handed to CRASH_SYSTEM
 *   (-0x254,A6) status        ACL / TIME_$WAIT / CAL / NETWORK status, and
 *                             the operand of the delay loop
 *   (-0x25a,A6) out_len       VFMT's output length
 *   (-0x25c,A6) wire_count    MST_$WIRE_AREA's page count
 *
 * Constant cells (`pea (d,PC)`), image bytes:
 *   0x00E6D628  00 00        TIME_$WAIT delay type; also FILE_$PRIV_UNLOCK_ALL's argument
 *   0x00E6D62A  00 02        NETWORK_$SET_SERVICE op
 *   0x00E6D62C  "shutdown failed. status = %lh%%%." 00
 *   0x00E6D64E  00 64        VFMT max length AND MST_$WIRE_AREA's max-pages
 *   0x00E6D650  "Beginning shutdown sequence...%."
 *   0x00E6D670  "Shutdown successful%"
 *   0x00E6D684  00 00 00 00  VFMT's padding argument
 *   0x00E6D688  00 E8 21 28  PTR_OS_DATA_SHUTWIRED
 *   0x00E6D68C  00 E8 27 40  PTR_OS_DATA_SHUTWIRED_END
 *   0x00E6D690  00 E5 D0 50  PTR_OS_PROC_SHUTWIRED
 *   0x00E6D694  00 E6 D6 FE  PTR_OS_PROC_SHUTWIRED_END
 *
 * Re-emitted from the disassembly 2026-09-27: the previous C waited on a
 * zero local instead of OS_$SHUTDOWN_WAIT_TIME, handed MST_$WIRE_AREA the
 * wait record and the page list in place of &100 and &wire_count, and
 * used invented text for the format and the two console strings.
 */

#include "os/os_internal.h"
#include "misc/crash_system.h"
#include "vfmt/vfmt.h"

/* 0x00E6D628: 00 00 */
static const uint16_t os_$shutdown_wait_type_00e6d628 = 0;
/* 0x00E6D62A: 00 02 (NETWORK_OP_SET_VALUE).  Non-const only because
 * NETWORK_$SET_SERVICE's first parameter is a plain int16_t *; it only
 * reads it (0x00E0F478 `move.w (A0),D2w`). */
static int16_t os_$shutdown_net_op_00e6d62a = NETWORK_OP_SET_VALUE;
/* 0x00E6D62C */
static const char os_$shutdown_fmt_failed_00e6d62c[] = "shutdown failed. status = %lh%%%.";
/* 0x00E6D64E: 00 64 */
static const int16_t os_$shutdown_hundred_00e6d64e = 100;
/* 0x00E6D650: the '.' after the '%' is the next image byte, not part of
 * the message (crash_puts_string stops at '%') */
static const char os_$shutdown_msg_begin_00e6d650[] = "Beginning shutdown sequence...%.";
/* 0x00E6D670 */
static const char os_$shutdown_msg_done_00e6d670[] = "Shutdown successful%";
/* 0x00E6D684: 00 00 00 00 */
static const uint32_t os_$shutdown_zero_00e6d684 = 0;

void OS_$SHUTDOWN(status_$t *status_p)
{
    status_$t caller_status;        /* (-0x258,A6), never read */
    status_$t status;               /* (-0x254,A6) */
    status_$t volx_status;          /* (-0x250,A6) */
    uint32_t svc_value;             /* (-0x24c,A6) */
    uint32_t wire_pages[100];       /* (-0x248,A6) */
    uint8_t orig_sids[40];          /* (-0xb8,A6) */
    uid_t cur_sids;                 /* (-0x90,A6) */
    char line[100];                 /* (-0x68,A6) */
    int16_t out_len;                /* (-0x25a,A6) */
    int16_t wire_count;             /* (-0x25c,A6) */
    int16_t i;                      /* D1w */

    /* 0x00E6D482 .. 0x00E6D486 */
    caller_status = *status_p;
    (void)caller_status;

    /* 0x00E6D48A `cmpi.w #0x1,(0x00e20608).l` / `beq` */
    if (PROC1_$CURRENT != 1) {
        /* 0x00E6D494 .. 0x00E6D4A6: ACL_$GET_RE_SIDS(&orig_sids, &cur_sids, &status) */
        ACL_$GET_RE_SIDS(orig_sids, &cur_sids, &status);
        /* 0x00E6D4AA tst.l / bne.w 0x00e6d620 */
        if (status != status_$ok) {
            return;
        }
        /* 0x00E6D4B2 .. 0x00E6D4C6: two cmpm.l against 0xE17434 */
        if (cur_sids.high != RGYC_$G_LOCKSMITH_UID.high ||
            cur_sids.low != RGYC_$G_LOCKSMITH_UID.low) {
            return;
        }
    }

    /* 0x00E6D4CA .. 0x00E6D4DC: TIME_$WAIT(&type, &OS_$SHUTDOWN_WAIT_TIME, &status) */
    TIME_$WAIT((uint16_t *)&os_$shutdown_wait_type_00e6d628,
               &OS_$SHUTDOWN_WAIT_TIME, &status);

    /* 0x00E6D4E0 .. 0x00E6D4EA */
    CRASH_SHOW_STRING(os_$shutdown_msg_begin_00e6d650);

    /* 0x00E6D4EC st (0xc,A5) */
    OS_$SHUTTING_DOWN_FLAG = (char)0xFF;

    /* 0x00E6D4F0 .. 0x00E6D514 */
    NETWORK_$DISMISS_REQUEST_SERVERS();
    PROC2_$SHUTDOWN();
    ROUTE_$SHUTDOWN();
    PACCT_$SHUTDN();
    LOG_$SHUTDN();
    AUDIT_$SHUTDOWN();
    HINT_$SHUTDN();

    /* 0x00E6D51A `tst.b (0x00e24c4a).l` / `bmi`: a diskless node skips the
     * calendar */
    if (NETWORK_$REALLY_DISKLESS >= 0) {
        CAL_$SHUTDOWN(&status);                             /* 0x00E6D522 */
    }

    /* 0x00E6D52E clr.l (0x00e218d0).l */
    FP_$SAVEP = 0;

    /* 0x00E6D534 .. 0x00E6D56C: wire the shutdown-wired code, then data */
    MST_$WIRE_AREA(&PTR_OS_PROC_SHUTWIRED, &PTR_OS_PROC_SHUTWIRED_END,
                   wire_pages, &os_$shutdown_hundred_00e6d64e, &wire_count);
    MST_$WIRE_AREA(&PTR_OS_DATA_SHUTWIRED, &PTR_OS_DATA_SHUTWIRED_END,
                   wire_pages, &os_$shutdown_hundred_00e6d64e, &wire_count);

    /* 0x00E6D570 .. 0x00E6D57A: FILE_$PRIV_UNLOCK_ALL(&the zero word) */
    FILE_$PRIV_UNLOCK_ALL((uint16_t *)&os_$shutdown_wait_type_00e6d628);

    /* 0x00E6D57C st (0x00e254da).l */
    PMAP_$SHUTTING_DOWN_FLAG = (char)0xFF;

    /* 0x00E6D582 */
    AREA_$SHUTDOWN();

    /* 0x00E6D588 clr.l (-0x250,A6) */
    volx_status = status_$ok;

    /* 0x00E6D58C `tst.b (0x00e24c4a).l` / `bmi.b 0x00e6d5d4` */
    if (NETWORK_$REALLY_DISKLESS >= 0) {
        /* 0x00E6D594 .. 0x00E6D5A4: VOLX_$SHUTDOWN twice, the second result kept */
        (void)VOLX_$SHUTDOWN();
        volx_status = VOLX_$SHUTDOWN();
        if (volx_status != status_$ok) {
            /* 0x00E6D5A6 .. 0x00E6D5C4: six arguments, the last a pad */
            VFMT_$FORMATN(os_$shutdown_fmt_failed_00e6d62c, line,
                          (int16_t *)&os_$shutdown_hundred_00e6d64e, &out_len,
                          &volx_status, &os_$shutdown_zero_00e6d684);
            /* 0x00E6D5C8 .. 0x00E6D5D2 */
            CRASH_SHOW_STRING(line);
        }
    }

    /* 0x00E6D5D4 .. 0x00E6D5DE */
    CRASH_SHOW_STRING(os_$shutdown_msg_done_00e6d670);

    /* 0x00E6D5E0 .. 0x00E6D5F6: NETWORK_$SET_SERVICE(&op 2, &0, &status) */
    svc_value = 0;
    NETWORK_$SET_SERVICE(&os_$shutdown_net_op_00e6d62a, &svc_value, &status);

    /* 0x00E6D5FA .. 0x00E6D612: `move.w #0x7d0,D1w` / dbf = 2001 rounds of
     * status = M$MIS$LLL(status, status) - a delay loop */
    for (i = 0x7d0; i != -1; i--) {
        status = (status_$t)M$MIS$LLL(status, status);
    }

    /* 0x00E6D616 .. 0x00E6D61A: CRASH_SYSTEM(&volx_status) */
    CRASH_SYSTEM(&volx_status);
}
