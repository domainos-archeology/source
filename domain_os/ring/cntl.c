/*
 * RINGLOG_$CNTL - ring logging control
 *
 * Original address: 0x00E72226, size 244 bytes (0x00E72226-0x00E7232B).
 * The two constant cells that follow the code are part of the same segment:
 *   0x00E7232C  word 0x000A        the MST_$WIRE_AREA page limit
 *   0x00E7232E  long 0x00EA3E38    the wire start VA (RINGLOG_$DATA)
 */

#include "ring/ring_internal.h"
#include "ring/ringlog_internal.h"

/*
 * ringlog_wire_start / ringlog_wire_max_pages - the two "pea (d,PC)" operands
 * of the MST_$WIRE_AREA call at 0x00E722C2 / 0x00E722D4, initialised with the
 * bytes at those addresses:
 *   00e72326  ...  00 0a 00 ea 3e 38  ...
 * i.e. 0x00E7232C = 0x000A and 0x00E7232E = 0x00EA3E38.
 */
static const uint32_t ringlog_wire_start    = RINGLOG_BUF_BASE;  /* 0x00E7232E */
static const uint16_t ringlog_wire_max_pages = 0x000A;           /* 0x00E7232C */

/*
 * RINGLOG_$CNTL
 *
 * Parameters (0x08, 0x0C, 0x10 off A6):
 *   cmd_ptr     - pointer to the command word
 *   param       - command-specific parameter / output buffer
 *   status_ret  - status return
 *
 * The jump table at 0x00E72250 (targets are 0x00E72250 + the table word):
 *   0 -> 0x003A   3 -> 0x003A   5 -> 0x003A   start, clear and wire
 *   1 -> 0x00B4   4 -> 0x00B4                 stop
 *   2 -> 0x00B8                               fall through to the copy
 *   6 -> 0x0012   7 -> 0x0020   8 -> 0x002E   set one socket filter
 * and "cmpi.w #0x9,D0w / bcc" sends anything above 8 to the same place as 2.
 */
void RINGLOG_$CNTL(uint16_t *cmd_ptr, void *param, status_$t *status_ret)
{
    uint16_t cmd = *cmd_ptr;
    int16_t  i;

    /* 0x00E72236: clr.l (A0) */
    *status_ret = status_$ok;

    switch (cmd) {
    case RINGLOG_CMD_START:             /* 0 */
    case RINGLOG_CMD_CLEAR:             /* 3 */
    case RINGLOG_CMD_START_FILTERED:    /* 5 */
        /*
         * 0x00E7228A.  The nested procedure borrows this frame's word at
         * (-0x2,A6) - the same cell the clear loop below uses - so it is
         * handed over explicitly.
         */
        RINGLOG_$STOP_LOGGING(&i);

        /* 0x00E7228E: clr.w (0x00ea3e38).l */
        ringlog_$set_index(0);

        /*
         * 0x00E72294-0x00E722B0:
         *   moveq #0x63,D0 / clr.w (-0x2,A6)
         *   movea.l #0xea3e38,A0
         *   moveq #0x2e,D1 / muls.w (-0x2,A6),D1
         *   andi.w #-0x5,(0xa,A0,D1*0x1)
         *   addq.w #0x1,(-0x2,A6) / dbf D0w
         * 100 entries based at RINGLOG_$DATA + 0 (not + 2), and the clear is a
         * WORD and on entry + 0x0A whose low byte is the flags byte at 0x0B
         * (bead source-wpuq).
         */
        for (i = 0; i < RINGLOG_MAX_ENTRIES; i++) {
            ringlog_$entry_t *entry = ringlog_$entry(i);

            entry->packed[RINGLOG_PACKED_OFF_08 + 3] &= (uint8_t)~RINGLOG_FLAG_VALID;
        }

        /* 0x00E722BA: clr.w (0x30,A1) */
        RINGLOG_$CTL.wire_count = 0;

        /*
         * 0x00E722BE-0x00E722DE: MST_$WIRE_AREA with five by-reference
         * arguments and no result slot ("lea (0x14,SP),SP"):
         *   arg 1  pea (0x58,PC)   -> 0x00E7232E, the start VA cell
         *   arg 2  pea (-0x8,A6)   -> a local holding 0xEA3E38 + 0x11FA
         *   arg 3  pea (A1)        -> RINGLOG_$CTL, used as the page list
         *   arg 4  pea (0x68,PC)   -> 0x00E7232C, the word page limit
         *   arg 5  pea (0x30,A1)   -> RINGLOG_$CTL.wire_count
         */
        {
            uint32_t wire_end = RINGLOG_WIRE_END;    /* A6-0x8 */

            MST_$WIRE_AREA(&ringlog_wire_start,
                           &wire_end,
                           &RINGLOG_$CTL,
                           &ringlog_wire_max_pages,
                           &RINGLOG_$CTL.wire_count);
        }

        /* 0x00E722E2-0x00E722FC: the ID filter, taken from *param for cmd 5 */
        if (*cmd_ptr == RINGLOG_CMD_START_FILTERED) {
            RINGLOG_$ID = *(uint32_t *)param;
        } else {
            RINGLOG_$ID = 0;
        }

        /* 0x00E722FE: st (0x38,A1) */
        RING_$LOGGING_NOW = (int8_t)0xFF;
        break;

    case RINGLOG_CMD_STOP_COPY:         /* 1 */
    case RINGLOG_CMD_STOP:              /* 4 */
        /* 0x00E72304 */
        RINGLOG_$STOP_LOGGING(&i);
        break;

    case RINGLOG_CMD_SET_NIL_SOCK:      /* 6 */
        /* 0x00E72262: move.b (A3),(0x36,A1) */
        RINGLOG_$NIL_SOCK = *(int8_t *)param;
        break;

    case RINGLOG_CMD_SET_WHO_SOCK:      /* 7 */
        /* 0x00E72270: move.b (A3),(0x34,A1) */
        RINGLOG_$WHO_SOCK = *(int8_t *)param;
        break;

    case RINGLOG_CMD_SET_MBX_SOCK:      /* 8 */
        /* 0x00E7227E: move.b (A3),(0x32,A1) */
        RINGLOG_$MBX_SOCK = *(int8_t *)param;
        break;

    default:
        /* 0x00E72242: anything >= 9 joins command 2 at 0x00E72308 */
        break;
    }

    /*
     * 0x00E72308-0x00E72320:
     *   move.w (A2),D0w / moveq #0x7,D1 / btst.l D0,D1 / beq
     *   movea.l #0xea3e38,A0 / lea (A3),A1
     *   move.w #0x47e,D0w / move.l (A0)+,(A1)+ / dbf D0w
     * i.e. commands 0, 1 and 2 copy 0x47F longwords - 0x11FC bytes, the whole
     * of RINGLOG_$DATA per the SAU2 map.  btst on a data register takes the
     * bit number modulo 32.
     */
    if ((1u << (*cmd_ptr & 0x1F)) & 0x7) {
        const uint8_t *src = RINGLOG_$DATA.bytes;
        uint8_t       *dst = (uint8_t *)param;

        for (i = 0; i < (int16_t)RINGLOG_DATA_SIZE; i++) {
            dst[i] = src[i];
        }
    }
}
