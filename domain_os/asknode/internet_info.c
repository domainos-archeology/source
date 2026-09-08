/*
 * ASKNODE_$INTERNET_INFO - Get detailed node information
 *
 * Main function for querying node information over the network.
 * This is a large function (4796 bytes) that handles many different
 * request types for various kinds of system data.
 *
 * For local node queries (node_id == NODE_$ME or node_id == 0), it
 * retrieves data directly from system structures.
 *
 * For remote nodes, it sends a network request using PKT_$SAR_INTERNET
 * and waits for a response.
 *
 * Original address: 0x00E645EA
 * Size: 4796 bytes
 *
 * The local dispatch is one long cmpi.w chain at 0x00E6463E-0x00E64786;
 * every arm below cites the address range of its own code, and the chain's
 * fall-through at 0x00E64786 is the default arm at 0x00E65502.
 */

#include "asknode/asknode_internal.h"
#include "misc/string.h"

/*
 * Globals: NETWORK_$*_CNT statistics and NETWORK_$CAPABLE_FLAGS come from
 * network/network.h, MEM_$MEM_REC from mem/mem.h, MMAP_$REAL_PAGES from
 * mmap/mmap.h, RING_$CTL / RING_$DATA / RING_$SWDIAG_DATA from ring/ring.h,
 * ROUTE_$PORTP and the routing counters from route/route.h, RIP_$INFO and
 * RIP_$STATS from rip/rip.h and the status_$network_* codes from
 * network/network.h (all via asknode_internal.h).
 */

/*
 * ============================================================================
 * Pascal by-reference constant cells
 * ============================================================================
 *
 * Domain Pascal passes value parameters of by-reference routines through
 * anonymous constants in the code segment, reached with "pea (d,PC)".  The
 * whole pool this function uses sits at 0x00E658AE..0x00E658CD, immediately
 * after the last instruction; `gsk read 0xE658AE 32` gives
 *
 *   00e658ae  00 39 00 00 00 00 ff ff  ff ff 00 00 00 2a 00 02
 *   00e658be  00 01 00 04 00 03 00 00  01 f8 00 13 00 3e 00 00
 *
 * Each cell is named below for the argument it supplies; the pea site that
 * reaches it is given with the displacement resolved (pea's PC is the
 * instruction address + 2).
 */

/* 0x00E658AE = 57: the process-list capacity PROC2_$LIST and
 * PROC2_$ZOMBIE_LIST are given (pea (0xF1A,PC) at 0x00E64992,
 * pea (0xED6,PC) at 0x00E649D6). */
static const uint16_t asknode_$c_max_procs = 0x0039;

/* 0x00E658B0, ten bytes: the route record the request-0x41 arm reports for
 * the local network - next hop 0, expiration -1, metric 0
 * (lea (0x64E,PC),A0 at 0x00E65260, copied as move.l/move.l/move.w). */
static const uint32_t asknode_$c_local_route_nexthop    = 0x00000000u;
static const uint32_t asknode_$c_local_route_expiration = 0xFFFFFFFFu;
static const uint16_t asknode_$c_local_route_metric     = 0x0000;

/* 0x00E658BA = 42: the size DISK_$GET_MNT_INFO is told its record has
 * (pea (0xBCC,PC) at 0x00E64CEC and pea (0xB48,PC) at 0x00E64D70). */
static const uint16_t asknode_$c_mnt_info_size = 0x002A;

/* 0x00E658BC = 2 and 0x00E658BE = 1: the display units the request-0x27 arm
 * asks SMD_$INQ_DISP_INFO about (pea (0xC3A,PC) at 0x00E64C80 and
 * pea (0xC58,PC) at 0x00E64C64). */
static const uint16_t asknode_$c_disp_unit_2 = 0x0002;
static const uint16_t asknode_$c_disp_unit_1 = 0x0001;

/* 0x00E658C0 = 4 and 0x00E658C2 = 3: the RINGLOG_$CNTL commands the
 * request-0x25 arm issues (pea (0xCA8,PC) at 0x00E64C16 and
 * pea (0xCB6,PC) at 0x00E64C0A).  ring/ringlog.h calls them
 * RINGLOG_CMD_STOP and RINGLOG_CMD_CLEAR. */
static const uint16_t asknode_$c_ringlog_stop  = 0x0004;
static const uint16_t asknode_$c_ringlog_clear = 0x0003;

/* 0x00E658C4 = 0: PROC2_$INFO's scan key, always "no key" here
 * (pea (0xCEC,PC) at 0x00E64BD6). */
static const int16_t asknode_$c_zero = 0x0000;

/* 0x00E658C6 = 504: the PROC2_$GET_INFO / PROC2_$INFO record capacity
 * (pea (0xE84,PC) at 0x00E64A40 and pea (0xCF8,PC) at 0x00E64BCC). */
static const uint16_t asknode_$c_proc_info_len = 0x01F8;

/* 0x00E658C8 = 19: the signal number the request-0x16 and request-0x35 arms
 * deliver (pea (0xE9A,PC) at 0x00E64A2C and pea (0x910,PC) at 0x00E64FB6). */
static const int16_t asknode_$c_signal = 0x0013;

/* 0x00E658CA = 62: the process-list capacity PROC2_$LIST2 and the
 * request-0x59 PROC2_$ZOMBIE_LIST are given (pea (0xECE,PC) at 0x00E649FA
 * and pea (0xEAE,PC) at 0x00E64A1A). */
static const uint16_t asknode_$c_max_procs2 = 0x003E;

/* 0x00E658CC is the zero longword the remote path passes as "no request
 * data"; it is exported as ASKNODE_$EMPTY_DATA (asknode_internal.h). */

/*
 * ============================================================================
 * Reply-record accessors
 * ============================================================================
 *
 * The reply is a Pascal variant record: every request type lays a different
 * shape over the same bytes, so almost every field is addressed by byte
 * offset and many of them are not longword aligned (m68k only requires word
 * alignment).  These helpers copy whole fields, so they place exactly the
 * bytes the original move.b/move.w/move.l place and behave the same on a
 * little-endian host; nothing here splits a word or a longword into bytes.
 */
static uint16_t reply_get_w(const uint32_t *reply, unsigned off)
{
    uint16_t v;
    memcpy(&v, (const uint8_t *)reply + off, sizeof(v));
    return v;
}

static uint32_t reply_get_l(const uint32_t *reply, unsigned off)
{
    uint32_t v;
    memcpy(&v, (const uint8_t *)reply + off, sizeof(v));
    return v;
}

static void reply_put_b(uint32_t *reply, unsigned off, uint8_t v)
{
    ((uint8_t *)reply)[off] = v;
}

static void reply_put_w(uint32_t *reply, unsigned off, uint16_t v)
{
    memcpy((uint8_t *)reply + off, &v, sizeof(v));
}

static void reply_put_l(uint32_t *reply, unsigned off, uint32_t v)
{
    memcpy((uint8_t *)reply + off, &v, sizeof(v));
}

static void reply_put_bytes(uint32_t *reply, unsigned off, const void *src,
                            unsigned len)
{
    unsigned i;
    for (i = 0; i < len; i++) {
        ((uint8_t *)reply)[off + i] = ((const uint8_t *)src)[i];
    }
}

/* The request parameter block the caller supplies, addressed the same way. */
static uint16_t param_get_w(const uid_t *param, unsigned off)
{
    uint16_t v;
    memcpy(&v, (const uint8_t *)param + off, sizeof(v));
    return v;
}

static uint32_t param_get_l(const uid_t *param, unsigned off)
{
    uint32_t v;
    memcpy(&v, (const uint8_t *)param + off, sizeof(v));
    return v;
}

/*
 * Handle local node query for request type
 */
static uint32_t handle_local_request(uint16_t req_type, uid_t *param,
                                     uint32_t *result, status_$t *local_status,
                                     status_$t *status)
{
    uint32_t ret_val = 0;
    *local_status = 0;

    switch (req_type) {
    case ASKNODE_REQ_BOOT_TIME:   /* 0x02, 0x00E6495A-0x00E6496E */
        /* Return boot time and current time */
        result[2] = TIME_$BOOT_TIME;
        result[3] = TIME_$CURRENT_CLOCKH;
        break;

    case ASKNODE_REQ_NODE_UID:    /* 0x04, 0x00E6489A-0x00E64912 */
    case ASKNODE_REQ_ROOT_UID:    /* 0x18, same arm */
        {
            uid_t temp_uid;
            if (req_type == ASKNODE_REQ_NODE_UID) {
                NAME_$GET_NODE_UID(&temp_uid);
            } else {
                NAME_$GET_ROOT_UID(&temp_uid);
            }
            result[2] = temp_uid.high;
            result[3] = temp_uid.low;
            result[1] = 0;
            /*
             * 0x00E648CC-0x00E648D2:
             *   move.l (0x8,A1),D3 / neg.l D3 / move.l D3,(0x12,A1)
             * reply+0x12 = -(reply+0x08).  The remote arm of this same
             * function reads it back at 0x00E6581C as
             * "(0x12,A1) + (0x8,A1) == 0", i.e. it is the complement that
             * marks "this uid names the responding node itself"; the
             * companion word pair at +0x22 sums to 1 instead (0x00E657F6).
             */
            reply_put_l(result, 0x12, (uint32_t)(-(int32_t)result[2]));
            /* Set mother node or local node based on diskless flag */
            reply_put_l(result, 0x1E, reply_get_l(result, 0x1E) & 0xFFF00000u);
            if (NETWORK_$DISKLESS < 0) {
                reply_put_l(result, 0x1E,
                            reply_get_l(result, 0x1E) | NETWORK_$MOTHER_NODE);
            } else {
                reply_put_l(result, 0x1E,
                            reply_get_l(result, 0x1E) | NODE_$ME);
            }
            reply_put_l(result, 0x22, 1u - result[2]);
            reply_put_l(result, 0x26, ROUTE_$PORT);
        }
        break;

    case ASKNODE_REQ_STATS:       /* 0x06, 0x00E6478A-0x00E64846 */
        /* Return comprehensive node statistics */
        reply_put_w(result, 0x08, 3);
        reply_put_l(result, 0x0A, NODE_$ME);
        reply_put_w(result, 0x0E, 1);
        reply_put_w(result, 0x10, NETWORK_$INFO_RQST_CNT);
        reply_put_w(result, 0x12, (uint16_t)(NETWORK_$PAGIN_RQST_CNT +
                                             NETWORK_$MULT_PAGIN_RQST_CNT));
        reply_put_w(result, 0x14, NETWORK_$PAGOUT_RQST_CNT);
        reply_put_w(result, 0x16, NETWORK_$READ_CALL_CNT);
        reply_put_w(result, 0x18, NETWORK_$WRITE_CALL_CNT);
        reply_put_w(result, 0x1A, NETWORK_$READ_VIOL_CNT);
        reply_put_w(result, 0x1C, NETWORK_$WRITE_VIOL_CNT);
        reply_put_w(result, 0x1E, NETWORK_$BAD_CHKSUM_CNT);
        /*
         * Copy 15 longwords to reply+0x20 (0x00E647E6-0x00E647F6).  The
         * source the image names is 0xE261E0, which the SAU2 map calls
         * RING_$DATA (the per-unit statistics array) - NOT RING_$CTL at
         * 0xE86400.
         */
        {
            uint32_t *src = (uint32_t *)&RING_$DATA[0];
            uint32_t *dst = result + 8;
            int16_t i;
            for (i = 0; i < 15; i++) {
                *dst++ = *src++;
            }
        }
        /* Get disk stats (0x00E647FA-0x00E6480E) */
        {
            uint8_t has_stats;
            DISK_$GET_STATS(0, 0, 0, &has_stats, (uint8_t *)result + 0x5C);
        }
        /*
         * Copy MEM_$MEM_REC into the reply at +0x72 (0x00E64812-0x00E6482A):
         *
         *   00e64812  movea.l #0xe22934,A0     ; MEM_$MEM_REC
         *   00e6481c  moveq #0x14,D3
         *   00e6481e  lea (0x72,A1),A3
         *   00e64824  move.l (A0)+,(A3)+
         *   00e64826  dbf D3w,0x00e64824       ; 21 longwords
         *   00e6482a  move.w (A0)+,(A3)+       ; + one word
         *
         * 21*4 + 2 = 0x56 bytes, 0xE22934..0xE22989 - the whole record,
         * page-error table included (mem/mem.h).  The copy is expressed as a
         * whole-record assignment through a correctly typed destination
         * pointer: MEM_$MEM_REC is a field of the unpacked mem_data_t, so
         * neither side is an address-of-packed-member, and sizeof is exactly
         * the 0x56 bytes the loop moves.  Source and destination cannot
         * overlap, so the moves' order is not observable.
         *
         * The destination runs 0x72..0xC7, so it covers the +0x76 word the
         * next statement overwrites - the store order below is the image's.
         */
        *(mem_$mem_rec_t *)((char *)result + 0x72) = MEM_$MEM_REC;
        /* Real pages count (0x00E6482C-0x00E64846) */
        if (MMAP_$REAL_PAGES < 0x10000) {
            reply_put_w(result, 0x76, (uint16_t)MMAP_$REAL_PAGES);
        } else {
            reply_put_w(result, 0x76, 0);
        }
        break;

    case ASKNODE_REQ_TIMEZONE:    /* 0x08, 0x00E64866-0x00E6486C + 0x00E6537E */
        /*
         * Raw 12-byte copy of the timezone record (bytes 0..11 of
         * CAL_$TIMEZONE, done as three longword moves in the original at the
         * shared tail 0x00E6537E-0x00E65388).
         */
        reply_put_bytes(result, 0x08, &CAL_$TIMEZONE, 12);
        break;

    case ASKNODE_REQ_VOLUME_INFO: /* 0x0A, 0x00E64972-0x00E6498A */
        /* Note: param is uid_t* but VOLX_$GET_INFO expects int16_t* vol_idx.
         * The caller likely passes the volume index in the first field. */
        VOLX_$GET_INFO((int16_t *)param, (uid_t *)(result + 2),
                       result + 4, result + 5, local_status);
        break;

    case ASKNODE_REQ_PAGING_INFO: /* 0x0C, 0x00E64870-0x00E64896 */
        /* Return paging information */
        reply_put_b(result, 0x0C, (uint8_t)NETWORK_$DISKLESS);
        if (NETWORK_$DISKLESS < 0) {
            reply_put_l(result, 0x08, NETWORK_$PAGING_FILE_UID.low & 0xFFFFFu);
        } else {
            reply_put_l(result, 0x08, NODE_$ME);
        }
        break;

    case ASKNODE_REQ_DISK_STATS:  /* 0x10, 0x00E64916-0x00E64956 */
        /*
         * Four fixed-size statistics records, one per unit, packed 22 bytes
         * apart from reply+0x08 ("lea (0x16,A2),A2" at 0x00E6494E).  The
         * controller type is 4 and the controller number 0 for all four
         * ("move.l #0x40000,-(SP)" at 0x00E64928 pushes the two words).
         */
        {
            int16_t unit;
            for (unit = 0; unit < 4; unit++) {
                uint8_t has_stats;
                uint8_t stats[DISK_STATS_SIZE];
                DISK_$GET_STATS(4, 0, unit, &has_stats, stats);
                reply_put_bytes(result, 0x08 + (unsigned)unit * 0x16,
                                stats, DISK_STATS_SIZE);
            }
        }
        break;

    case ASKNODE_REQ_PROC_LIST:   /* 0x12, 0x00E6498E-0x00E649BE */
        PROC2_$LIST((uid_t *)((char *)result + 0x0A),
                    (uint16_t *)&asknode_$c_max_procs,
                    (uint16_t *)((char *)result + 0x08));
        /*
         * 0x00E649A8-0x00E649BE: protocol versions below 3 can only carry
         * 0x19 entries, so the count is clamped for them.  Both tests fall
         * through to the shared "local_status := 0" tail at 0x00E64F86.
         */
        if (reply_get_w(result, 0x00) <= 2 &&
            (int16_t)reply_get_w(result, 0x08) > 0x19) {
            reply_put_w(result, 0x08, 0x19);
        }
        *local_status = 0;
        break;

    case ASKNODE_REQ_PROC_INFO:   /* 0x14, 0x00E64A3C-0x00E64A50 */
        PROC2_$GET_INFO(param, (uint8_t *)result + 0x08,
                        (uint16_t *)&asknode_$c_proc_info_len, local_status);
        break;

    case ASKNODE_REQ_SIGNAL:      /* 0x16, 0x00E64A24-0x00E64A38 */
        PROC2_$SIGNAL_PGROUP_OS(param, (int16_t *)&asknode_$c_signal,
                                (uint32_t *)((uint8_t *)param + 0x08),
                                local_status);
        break;

    case ASKNODE_REQ_BUILD_TIME:  /* 0x1A, 0x00E64A54-0x00E64A62 */
        GET_BUILD_TIME((char *)result + 0x0A,
                       (int16_t *)((char *)result + 0x08));
        break;

    case ASKNODE_REQ_UIDS:        /* 0x1C, 0x00E64A66-0x00E64B28 */
        /*
         * Six well-known identifiers.  Each NAME_$ call writes a scratch uid
         * that the original then copies through a second scratch at A6-0x30
         * before it reaches the reply; the intermediate copy is kept because
         * it is what the code does.
         */
        {
            uid_t fetched;
            uid_t staged;

            reply_put_w(result, 0x08, 6);

            NAME_$GET_ROOT_UID(&fetched);
            staged = fetched;
            reply_put_l(result, 0x0A, staged.high);
            reply_put_l(result, 0x0E, staged.low);

            NAME_$GET_NODE_UID(&fetched);
            staged = fetched;
            reply_put_l(result, 0x12, staged.high);
            reply_put_l(result, 0x16, staged.low);

            NAME_$GET_NODE_DATA_UID(&fetched);
            staged = fetched;
            /* 0x00E64AD8: the reply status is cleared here, between the
             * third fetch and its store. */
            result[1] = 0;
            reply_put_l(result, 0x1A, staged.high);
            reply_put_l(result, 0x1E, staged.low);

            reply_put_l(result, 0x22, NETWORK_$PAGING_FILE_UID.high);
            reply_put_l(result, 0x26, NETWORK_$PAGING_FILE_UID.low);

            /*
             * 0x00E64AF6-0x00E64B1C: the low longword of the fifth uid keeps
             * its top 12 bits and takes the mother node (diskless) or this
             * node in the low 20.  Its high longword at +0x2A is left as the
             * caller supplied it, exactly as in the request-0x04 arm.
             */
            reply_put_l(result, 0x2E, reply_get_l(result, 0x2E) & 0xFFF00000u);
            if (NETWORK_$DISKLESS < 0) {
                reply_put_l(result, 0x2E,
                            reply_get_l(result, 0x2E) | NETWORK_$MOTHER_NODE);
            } else {
                reply_put_l(result, 0x2E,
                            reply_get_l(result, 0x2E) | NODE_$ME);
            }
            reply_put_l(result, 0x36, ROUTE_$PORT);
        }
        break;

    case ASKNODE_REQ_NETWORK_DIAG: /* 0x1F local, 0x00E64B2C-0x00E64BAC */
        /*
         * The local answer to the network-diagnostics request.  (A remote
         * one is served by NETWORK_$RING_INFO in the caller below, which is
         * why this arm has no counterpart there.)
         */
        reply_put_w(result, 0x08, 3);
        reply_put_b(result, 0x0A, (uint8_t)NETWORK_$DISKLESS);
        reply_put_l(result, 0x0C, NETWORK_$MOTHER_NODE);
        /* 15 longwords of RING_$DATA[0] (0x00E64B42-0x00E64B52) */
        {
            uint32_t *src = (uint32_t *)&RING_$DATA[0];
            int16_t i;
            for (i = 0; i < 15; i++) {
                reply_put_l(result, 0x10 + (unsigned)i * 4, src[i]);
            }
        }
        /* The whole 16-byte failure record (0x00E64B56-0x00E64B66) */
        reply_put_bytes(result, 0x4C, &NETWORK_$FAILURE_REC,
                        sizeof(network_$failure_rec_t));
        /* The 30-byte software-diagnostic block (0x00E64B68-0x00E64B7A) */
        reply_put_bytes(result, 0x5C, &RING_$SWDIAG_DATA,
                        sizeof(ring_$swdiag_t));
        /*
         * 0x00E64B7C-0x00E64BA4: five counters that overwrite parts of the
         * block just copied (+0x5E and +0x76 land inside it) plus four that
         * follow it.  ring/ring.h documents the -0x1A displacement between
         * each swdiag slot and its live counter.
         */
        reply_put_l(result, 0x5E, RING_$SWDIAG_RCVCNT);
        reply_put_l(result, 0x76, RING_$SWDIAG_NODEID);
        reply_put_w(result, 0x7A, RING_$XMIT_BIPHASE);
        reply_put_w(result, 0x7C, RING_$RCV_BIPHASE);
        reply_put_w(result, 0x7E, RING_$XMIT_ESB);
        reply_put_w(result, 0x80, RING_$RCV_ESB);
        break;

    case ASKNODE_REQ_PROC_INFO2:  /* 0x21, 0x00E64BB0-0x00E64BE0 */
        /*
         * 0x00E64BB0-0x00E64BC4: the pid must be 1..0x40; anything else is
         * "illegal process id" in the REPLY status, not the caller's.
         */
        {
            uint16_t pid = param_get_w(param, 0x00);
            if (pid == 0 || pid > 0x40) {
                *local_status = status_$illegal_process_id;
                break;
            }
            PROC2_$INFO((int16_t *)&asknode_$c_zero, (int16_t *)param,
                        (uint8_t *)result + 0x08,
                        (uint16_t *)&asknode_$c_proc_info_len, local_status);
        }
        break;

    case ASKNODE_REQ_PROC_UPIDS:  /* 0x23, 0x00E64BE4-0x00E64BFC */
        PROC2_$GET_UPIDS(param,
                         (uint16_t *)((uint8_t *)result + 0x08),
                         (uint16_t *)((uint8_t *)result + 0x0A),
                         (uint16_t *)((uint8_t *)result + 0x0C),
                         local_status);
        break;

    case ASKNODE_REQ_LOG_CONTROL: /* 0x25, 0x00E64C00-0x00E64C4A */
        /*
         * Two logging subsystems in one request.  The ring log is started
         * (command 3) when the first word of the parameter block is zero and
         * stopped (command 4) otherwise; both write the CALLER'S status, not
         * the reply's ("pea (A4)" at 0x00E64C04 / 0x00E64C10).  The network
         * log is only touched when the ring log succeeded (0x00E64C26).
         */
        {
            /*
             * A6-0x130.  RINGLOG_$CNTL does not read or write the parameter
             * block for commands 3 and 4 (ring/cntl.c), so the original
             * hands it an uninitialised frame slot.
             * TODO: recover the block's real size (bead source-ce2v).
             */
            uint32_t ringlog_param[3];
            uint32_t netlog_kinds;

            if (param_get_w(param, 0x00) == 0) {
                RINGLOG_$CNTL((uint16_t *)&asknode_$c_ringlog_clear,
                              ringlog_param, status);
            } else {
                RINGLOG_$CNTL((uint16_t *)&asknode_$c_ringlog_stop,
                              ringlog_param, status);
            }
            if (*status != 0) {
                break;
            }
            /* 0x00E64C2C: the kinds mask is staged in a frame cell because
             * NETLOG_$CNTL takes it by reference and the parameter block's
             * copy is not longword aligned. */
            netlog_kinds = param_get_l(param, 0x0A);
            /* The SAU2 map calls 0x00E71914 both NETLOG_$PROC_START and
             * NETLOG_$CNTL; they are the same entry point. */
            NETLOG_$CNTL((int16_t *)((uint8_t *)param + 0x02),
                         (uint32_t *)((uint8_t *)param + 0x04),
                         (uint16_t *)((uint8_t *)param + 0x08),
                         &netlog_kinds, status);
        }
        break;

    case ASKNODE_REQ_SYSTEM_INFO: /* 0x27, 0x00E64C4E-0x00E64DDE */
        {
            smd_disp_info_result_t disp_info;
            disk_$mnt_info_t mnt;
            status_$t sub_status;
            uint16_t peb_flags;
            uint8_t  peb_byte;
            uint32_t real_pages;
            int16_t  volx;
            int16_t  i;

            reply_put_w(result, 0x08, 0x11);
            reply_put_l(result, 0x0A, PROM_$MACHINE_ID);

            /*
             * 0x00E64C5C-0x00E64C8E: display unit 1, and unit 2 as well when
             * unit 1 reports display type 0.
             */
            SMD_$INQ_DISP_INFO((uint16_t *)&asknode_$c_disp_unit_1,
                               &disp_info, &sub_status);
            if (disp_info.display_type == 0) {
                SMD_$INQ_DISP_INFO((uint16_t *)&asknode_$c_disp_unit_2,
                                   &disp_info, &sub_status);
            }
            reply_put_w(result, 0x0E, disp_info.display_type);

            /* 0x00E64C98-0x00E64CB0: the PEB flags word (peb/peb.h). */
            PEB_$GET_INFO(&peb_flags, &peb_byte);
            reply_put_w(result, 0x10, peb_flags);

            /* 0x00E64CB4-0x00E64CCA */
            real_pages = MMAP_$REAL_PAGES;
            if (real_pages < 0x10000) {
                reply_put_w(result, 0x12, (uint16_t)real_pages);
            } else {
                reply_put_w(result, 0x12, 0);
            }

            reply_put_b(result, 0x14, (uint8_t)NETWORK_$DISKLESS);
            reply_put_l(result, 0x16,
                        NETWORK_$PAGING_FILE_UID.low & 0xFFFFFu);

            /*
             * 0x00E64CE4-0x00E64D18: the boot volume.  The three tests are
             *   tst.w (-0x48,A6) / bpl  -> clear      flags byte not negative
             *   tst.l (-0x124,A6) / bne -> clear      the call failed
             *   tst.w (-0x68,A6) / beq  -> SKIP       device type zero
             * so the word at reply+0x1A survives only when the record came
             * back with a negative flags byte, a good status AND a device
             * type of zero; every other combination zeroes it.
             */
            DISK_$GET_MNT_INFO((uint16_t *)&CAL_$BOOT_VOLX,
                               (uint16_t *)&asknode_$c_mnt_info_size,
                               &mnt, &sub_status);
            if ((int8_t)mnt.flags >= 0 || sub_status != 0 ||
                mnt.dev_type != 0) {
                reply_put_w(result, 0x1A, 0);
            }

            reply_put_l(result, 0x1C, MMU_$SYSTEM_REV);   /* 0x00E64D20 */
            if (GPU_$PRESENT < 0) {                       /* 0x00E64D28 */
                reply_put_w(result, 0x20, 1);
            } else {
                reply_put_w(result, 0x20, 0);
            }

            /* 0x00E64D3C-0x00E64D52 */
            IO_$GET_CONFIG((uint16_t *)((uint8_t *)result + 0x22),
                           (uint16_t *)((uint8_t *)result + 0x24),
                           (uint16_t *)((uint8_t *)result + 0x26),
                           (uint16_t *)((uint8_t *)result + 0x28));

            /*
             * 0x00E64D5A-0x00E64DCC: volumes 1..10.  A volume is reported
             * when its device type is 0 or 4, its flags byte is negative,
             * the call succeeded and bit 6 of that byte (the logical-volume
             * bit DISK_$GET_MNT_INFO sets at 0x00E6BEBC) is clear.  The
             * count at +0x2A is incremented FIRST, so the first entry lands
             * at +0x2C and +0x3C - the two arrays overlap in the image and
             * are reproduced as they are.
             */
            reply_put_w(result, 0x2A, 0);
            volx = 1;
            for (i = 0; i < 10; i++) {
                uint16_t unit_volx = (uint16_t)volx;
                DISK_$GET_MNT_INFO(&unit_volx,
                                   (uint16_t *)&asknode_$c_mnt_info_size,
                                   &mnt, &sub_status);
                if ((mnt.dev_type == 0 || mnt.dev_type == 4) &&
                    (int8_t)mnt.flags < 0 && sub_status == 0 &&
                    (mnt.flags & DISK_MNT_FLAG_LOGICAL_VOLUME) == 0) {
                    uint16_t count = (uint16_t)(reply_get_w(result, 0x2A) + 1);
                    reply_put_w(result, 0x2A, count);
                    reply_put_w(result, 0x2A + (unsigned)count * 2,
                                mnt.dev_type);
                    reply_put_w(result, 0x3A + (unsigned)count * 2,
                                mnt.unit_id);
                }
                volx++;
            }

            /* 0x00E64DD0-0x00E64DDE */
            reply_put_l(result, 0x4C, real_pages);
            reply_put_w(result, 0x50, 1);
        }
        break;

    case ASKNODE_REQ_NET_STATS:   /* 0x29, 0x00E64DE2-0x00E64F02 */
        reply_put_w(result, 0x08, 1);
        /* Nine paging-backlog buckets (0x00E64DE8-0x00E64DF6) */
        {
            int16_t i;
            for (i = 0; i < NETWORK_PAGING_BACKLOG_BUCKETS; i++) {
                reply_put_l(result, 0x0A + (unsigned)i * 4,
                            NETWORK_$PAGING_BACKLOG[i]);
            }
        }
        reply_put_w(result, 0x2E, RING_$PAGING_OVERFLOW);
        /* Nine file-backlog buckets (0x00E64E02-0x00E64E12) */
        {
            int16_t i;
            for (i = 0; i < NETWORK_FILE_BACKLOG_BUCKETS; i++) {
                reply_put_l(result, 0x30 + (unsigned)i * 4,
                            NETWORK_$FILE_BACKLOG[i]);
            }
        }
        reply_put_w(result, 0x54, RING_$FILE_OVERFLOW);
        reply_put_w(result, 0x56, RING_$OVERFLOW_OVERFLOW);
        reply_put_w(result, 0x58, RING_$DELIVERY_FAILED);
        reply_put_w(result, 0x5A, NETWORK_$2LONG1);
        reply_put_w(result, 0x5C, (uint16_t)(NETWORK_$ATTRIB_RQST_CNT +
                                             NETWORK_$PAGOUT_RQST_CNT));
        reply_put_w(result, 0x5E, REM_FILE_$2LONG1);
        reply_put_l(result, 0x60, 0);
        reply_put_l(result, 0x64, RING_$DATA[0].xmitcnt);   /* 0xE261E6 */
        reply_put_w(result, 0x68, RING_$XMIT_WAITED);
        reply_put_w(result, 0x6A, RING_$SEND_NULL_CNT);
        reply_put_w(result, 0x6C, RING_$CLOBBERED_HDR);
        reply_put_l(result, 0x6E, RING_$DATA[0].rcvcnt);    /* 0xE261FC */
        reply_put_l(result, 0x72, RING_$RCV_INT_CNT);
        reply_put_w(result, 0x76, RING_$BUSY_ON_RCV_INT);
        reply_put_w(result, 0x78, RING_$ABORT_CNT);
        reply_put_w(result, 0x7A, RING_$WAKEUP_CNT);
        reply_put_w(result, 0x7C, RING_$BAD_DATA_CNT);
        /* 0x00E64EA2 stores the info count out of order, at +0x90. */
        reply_put_w(result, 0x90, NETWORK_$INFO_RQST_CNT);
        reply_put_w(result, 0x7E, NETWORK_$PAGIN_RQST_CNT);
        reply_put_w(result, 0x80, NETWORK_$MULT_PAGIN_RQST_CNT);
        reply_put_w(result, 0x82, NETWORK_$PAGOUT_RQST_CNT);
        reply_put_w(result, 0x84, NETWORK_$READ_CALL_CNT);
        reply_put_w(result, 0x86, NETWORK_$WRITE_CALL_CNT);
        reply_put_w(result, 0x88, NETWORK_$READ_VIOL_CNT);
        reply_put_w(result, 0x8A, NETWORK_$WRITE_VIOL_CNT);
        reply_put_w(result, 0x8C, NETWORK_$BAD_CHKSUM_CNT);
        reply_put_w(result, 0x8E, NETWORK_$ATTRIB_RQST_CNT);
        reply_put_w(result, 0x92, NETWORK_$SET_ATTRIB_CALL_CNT);
        reply_put_w(result, 0x94, NETWORK_$RCV_READ_AHEAD);
        break;

    case ASKNODE_REQ_PROC_PID:    /* 0x2B, 0x00E64F06-0x00E64F1A */
        reply_put_w(result, 0x08, PROC2_$GET_PID(param, local_status));
        break;

    case ASKNODE_REQ_FAILURE_REC: /* 0x2F, 0x00E64F1E-0x00E64F38 + 0x00E65382 */
        /*
         * While the network is active the "failure recorded" flag is cleared
         * before the record is handed out, so a reader always sees the state
         * as of this call ("clr.b (0x00E24BF6).l" at 0x00E64F26 - that byte
         * is network_$failure_rec_t.flag).
         */
        if (NETWORK_$ACTIVITY_FLAG < 0) {
            NETWORK_$FAILURE_REC.flag = 0;
        }
        reply_put_bytes(result, 0x08, &NETWORK_$FAILURE_REC,
                        sizeof(network_$failure_rec_t));
        break;

    case ASKNODE_REQ_LOG_READ:    /* 0x31, 0x00E64F3C-0x00E64F74 */
        /*
         * 0x00E64F3C: "btst.b #0x0,(0x1,A3)" is bit 16 of the parameter
         * block's first longword.
         */
        if ((param_get_l(param, 0x00) & 0x10000u) != 0) {
            /* Read by entry index */
            LOG_$READ2((uint8_t *)result + 0x0A, param_get_w(param, 0x02),
                       0x400, (uint16_t *)((uint8_t *)result + 0x08));
            *local_status = (*local_status & 0xFFFF0000u) | 0xFFFFu;
        } else {
            /* Read by line number - param points to max_len */
            LOG_$READ((uint8_t *)result + 0x0A, (uint16_t *)param,
                      (uint16_t *)((uint8_t *)result + 0x08));
        }
        break;

    case ASKNODE_REQ_PROC1_LIST:  /* 0x33, 0x00E64F78-0x00E64F84 -> 0x00E64F86 */
        PROC1_$GET_LIST((int16_t *)((uint8_t *)result + 0x08),
                        (proc_list_entry_t *)((uint8_t *)result + 0x0A));
        *local_status = 0;
        break;

    case ASKNODE_REQ_SIGNAL2:     /* 0x35, 0x00E64F8E-0x00E64FC2 */
        /*
         * A four-way jump table at 0x00E64FA6 on the longword at param+0x0C:
         *   0, 1 -> 0x00E64FAE, PROC2_$SIGNAL_OS
         *   2, 3 -> 0x00E64A24, the request-0x16 arm's PROC2_$SIGNAL_PGROUP_OS
         * Anything else falls out of range at 0x00E64F98 and takes the
         * default arm at 0x00E65502, which writes the CALLER'S status.
         */
        {
            uint32_t selector = param_get_l(param, 0x0C);
            if (selector >= 4) {
                *status = status_$network_unknown_request_type;
                break;
            }
            if (selector < 2) {
                PROC2_$SIGNAL_OS(param, (int16_t *)&asknode_$c_signal,
                                 (uint32_t *)((uint8_t *)param + 0x08),
                                 local_status);
            } else {
                PROC2_$SIGNAL_PGROUP_OS(param, (int16_t *)&asknode_$c_signal,
                                        (uint32_t *)((uint8_t *)param + 0x08),
                                        local_status);
            }
        }
        break;

    case ASKNODE_REQ_ROUTE_PORT:  /* 0x37, 0x00E64FC6-0x00E64FCE */
        reply_put_l(result, 0x08, ROUTE_$PORT);
        break;

    case ASKNODE_REQ_PORT_LIST:   /* 0x39, 0x00E64FD2-0x00E65084 */
        /*
         * Two passes over the eight ROUTE_$PORTP slots.  The first packs a
         * 6-byte record (port type word + socket longword) per open port
         * from reply+0x0A; the second appends an 8-byte record per open port
         * straight after them.  A port counts as open when its routing
         * capability word is non-zero ("tst.w (0x2c,A3)" at 0x00E64FF2).
         *
         * The count at +0x08 is bumped before each store, so entry n (1
         * based) sits at reply + 4 + 6n; the second pass starts from
         * reply + 8 + 6*count and steps 8 bytes, writing 6 bytes back from
         * the stepped pointer ("addq.l #0x8,A1" then "(-0x6,A1)" /
         * "(-0x2,A1)" at 0x00E65068-0x00E65076), i.e. immediately after the
         * first pass's last record.
         */
        {
            int16_t i;
            uint16_t count;
            unsigned tail;

            reply_put_w(result, 0x00, 4);
            reply_put_w(result, 0x08, 0);

            for (i = 0; i < 8; i++) {
                route_$port_t *port = ROUTE_$PORTP[i];
                if (port->active == 0) {
                    continue;
                }
                count = (uint16_t)(reply_get_w(result, 0x08) + 1);
                reply_put_w(result, 0x08, count);
                reply_put_w(result, 0x04 + (unsigned)count * 6,
                            port->port_type);
                reply_put_l(result, 0x06 + (unsigned)count * 6,
                            (uint32_t)(int32_t)(int16_t)port->socket);
            }

            count = reply_get_w(result, 0x08);
            tail = 0x08 + (unsigned)count * 6;
            for (i = 0; i < 8; i++) {
                route_$port_t *port = ROUTE_$PORTP[i];
                const uint8_t *driver;
                if (port->active == 0) {
                    continue;
                }
                tail += 8;
                driver = (const uint8_t *)ARCH_VA_TO_PTR(port->driver_info);
                reply_put_bytes(result, tail - 6, driver + 0x48, 8);
            }
        }
        break;

    case ASKNODE_REQ_PORT_INFO:   /* 0x3B, 0x00E65088-0x00E650C2 */
        {
            int16_t port_index = ROUTE_$FIND_PORT(param_get_w(param, 0x00),
                                                  (int32_t)param_get_l(param, 0x02));
            if (port_index == -1) {
                /* shares the error tail at 0x00E65162 with request 0x3D/0x5B */
                *local_status = status_$internet_unknown_network_port;
                break;
            }
            ROUTE_$SHORT_PORT(ROUTE_$PORTP[port_index],
                              (route_$short_port_t *)((uint8_t *)result + 0x08));
        }
        break;

    case ASKNODE_REQ_ROUTE_STATS: /* 0x3F, 0x00E650C6-0x00E65146 */
        /*
         * Two Domain booleans say whether the node is routing at all; the
         * counters are only filled in when at least one of them is set
         * ("sgt" at 0x00E650D4 / 0x00E650E2 and "or.b / bpl" at
         * 0x00E650E8).
         */
        {
            int8_t routing     = (ROUTE_$N_ROUTING_PORTS > 1) ? (int8_t)0xFF : 0;
            int8_t std_routing = (ROUTE_$STD_N_ROUTING_PORTS > 1) ? (int8_t)0xFF : 0;

            reply_put_w(result, 0x08, 3);
            reply_put_b(result, 0x0A, (uint8_t)routing);
            reply_put_b(result, 0x0B, (uint8_t)std_routing);
            if ((int8_t)(routing | std_routing) >= 0) {
                break;
            }
            reply_put_l(result, 0x0C, ROUTE_$START_TIME);
            reply_put_w(result, 0x10, ROUTE_$NETBUF_ALLOC);
            reply_put_l(result, 0x1E, ROUTE_$PKTS_ROUTED);
            reply_put_l(result, 0x12, ROUTE_$Q_OFLO);
            reply_put_l(result, 0x16, ROUTE_$MISROUTE);
            reply_put_l(result, 0x1A, ROUTE_$TOO_FAR);
            reply_put_l(result, 0x22, ROUTE_$DLEN_ERR);
            reply_put_l(result, 0x2E, ROUTE_$STD_PKTS_ROUTED);
            reply_put_l(result, 0x26, ROUTE_$STD_MISROUTE);
            reply_put_l(result, 0x2A, ROUTE_$STD_TOO_FAR);
            reply_put_l(result, 0x32, ROUTE_$STD_DLEN_ERR);
        }
        break;

    case ASKNODE_REQ_DEVICE_STAT:  /* 0x3D, 0x00E6514A-0x00E65248 */
    case ASKNODE_REQ_DEVICE_STAT2: /* 0x5B, same arm */
        {
            uint16_t network = param_get_w(param, 0x00);
            int16_t port_index = ROUTE_$FIND_PORT(network,
                                                  (int32_t)param_get_l(param, 0x02));
            route_$port_t *port;
            const uint8_t *driver;
            uint8_t id_buf[8];

            if (port_index == -1) {
                *local_status = status_$internet_unknown_network_port;
                break;
            }
            port = ROUTE_$PORTP[port_index];

            /*
             * 0x00E65184-0x00E65198: the port's own statistics window.  The
             * longword at +0x4E and the word at +0x52 straddle the two
             * longwords NET_IO_$CREATE_PORT writes, so route/route.h keeps
             * that block as words - see route_$port_t._unknown2b..2d.
             */
            reply_put_l(result, 0x08,
                        ((uint32_t)port->_unknown2b << 16) | port->_unknown2c);
            reply_put_w(result, 0x0C, port->_unknown2d);
            reply_put_l(result, 0x0E, port->stat_long_54);
            reply_put_l(result, 0x12, port->forward_count);
            /* 0x00E6519C-0x00E651AC: the offset of the driver block that
             * follows, computed as (reply+0x26) - (reply+0x08). */
            reply_put_w(result, 0x18, 0x1E);

            if (req_type == ASKNODE_REQ_DEVICE_STAT) {
                NET_IO_$DEVICE_STAT(network, param_get_w(param, 0x04), 0x80,
                                    id_buf, (uint8_t *)result + 0x26,
                                    (uint16_t *)((uint8_t *)result + 0x16),
                                    local_status);
            } else {
                NET_IO_$DEVICE_STAT2(network, param_get_w(param, 0x04), 0x80,
                                     id_buf, (uint8_t *)result + 0x26,
                                     (uint16_t *)((uint8_t *)result + 0x16),
                                     local_status);
            }

            reply_put_bytes(result, 0x1A, id_buf, 8);

            /*
             * 0x00E65218-0x00E65236: network 0 is the ring, so the receive
             * count reported at +0x0E comes from RING_$DATA[unit] instead of
             * the port record.  The index arithmetic is 60 * unit
             * ("lsl.w #0x2 / neg.w / lsl.w #0x4 / add.w"), i.e. the
             * ring_$stats_t stride.
             */
            if (network == 0) {
                uint16_t unit = param_get_w(param, 0x04);
                reply_put_l(result, 0x0E, RING_$DATA[unit].rcvcnt);
            }

            /* 0x00E65238-0x00E65248 */
            driver = (const uint8_t *)ARCH_VA_TO_PTR(port->driver_info);
            {
                uint16_t dev_id;
                memcpy(&dev_id, driver + 0x02, sizeof(dev_id));
                reply_put_l(result, 0x22, (uint32_t)dev_id);
            }
        }
        break;

    case ASKNODE_REQ_NET_ROUTE:   /* 0x41, 0x00E6524C-0x00E65324 */
        /*
         * The route to one network.  The local network is answered from
         * ROUTE_$PORT and a canned "never expires" route; any other network
         * is looked up in RIP_$INFO, and a network with no VALID/AGING route
         * (state bits 6..7 of routes[0].flags zero) is skipped just like an
         * unused slot.  Falling off the end of the table leaves the
         * "unknown network" status the scan started with.
         */
        {
            uint32_t wanted = param_get_l(param, 0x00);
            int16_t i;

            if (wanted == 0 || wanted == ROUTE_$PORT) {
                reply_put_l(result, 0x08, ROUTE_$PORT);
                reply_put_l(result, 0x0C, asknode_$c_local_route_nexthop);
                reply_put_l(result, 0x10, asknode_$c_local_route_expiration);
                reply_put_w(result, 0x14, asknode_$c_local_route_metric);
                reply_put_l(result, 0x16, RIP_$STATS.local_net_pkts);
                ROUTE_$SHORT_PORT(ROUTE_$PORTP[0],
                                  (route_$short_port_t *)
                                      ((uint8_t *)result + 0x1C));
                reply_put_w(result, 0x1A, 1);
                break;
            }

            *local_status = status_$network_unknown_network;
            for (i = 0; i < RIP_TABLE_SIZE; i++) {
                rip_$entry_t *entry = &RIP_$INFO[i];
                uint16_t state;

                if (entry->network != wanted) {
                    continue;
                }
                state = (uint16_t)((entry->routes[0].flags & RIP_STATE_MASK) >>
                                   RIP_STATE_SHIFT);
                if (state == 0) {
                    continue;
                }
                reply_put_l(result, 0x08, entry->network);
                {
                    /*
                     * 0x00E652C6: "and.l (0xe,A2),D4" reads the longword at
                     * entry+0x0E, which is routes[0].nexthop + 0x06, i.e.
                     * the last four bytes of the 6-byte XNS host address.
                     */
                    uint32_t host_lo;
                    memcpy(&host_lo, &entry->routes[0].nexthop.host[2],
                           sizeof(host_lo));
                    reply_put_l(result, 0x0C, host_lo & 0xFFFFFu);
                }
                reply_put_l(result, 0x10, entry->routes[0].expiration);
                reply_put_w(result, 0x14, entry->routes[0].metric);
                reply_put_w(result, 0x1A, state);
                reply_put_l(result, 0x16, RIP_$STATS.net_pkts[i]);
                ROUTE_$SHORT_PORT(ROUTE_$PORTP[entry->routes[0].port],
                                  (route_$short_port_t *)
                                      ((uint8_t *)result + 0x1C));
                *local_status = 0;      /* 0x00E65316 -> 0x00E64F86 */
                break;
            }
        }
        break;

    case ASKNODE_REQ_QUEUE_DEPTH: /* 0x43, 0x00E65328-0x00E65352 */
        /*
         * The through-traffic queue-depth histogram.  The loop bound is the
         * netbuf allocation the reply just reported, and dbf runs it one
         * more time than that ("move.w (0x8,A1),D0w" at 0x00E6533E feeding
         * the dbf at 0x00E6534E), so ROUTE_$NETBUF_ALLOC + 1 buckets reach
         * the reply.
         */
        {
            uint16_t buckets = ROUTE_$NETBUF_ALLOC;
            uint32_t i;

            reply_put_w(result, 0x08, buckets);
            reply_put_l(result, 0x0A, ROUTE_$Q_OFLO);
            for (i = 0; i <= (uint32_t)buckets; i++) {
                reply_put_l(result, 0x0E + i * 4, ROUTE_$Q_DEPTH[i]);
            }
        }
        break;

    case ASKNODE_REQ_BOOT_DEVICE: /* 0x47, 0x00E65356-0x00E65368 */
        reply_put_bytes(result, 0x08, &OS_$BOOT_DEVICE,
                        sizeof(os_$boot_device_t));
        break;

    case ASKNODE_REQ_LOADAV:      /* 0x49, 0x00E6536C-0x00E65388 */
        /*
         * PROC1_$GET_LOADAV fills three longwords; they reach the reply
         * through the shared 12-byte tail at 0x00E6537E that the timezone
         * arm also uses.
         */
        {
            uint32_t loadav[3];
            PROC1_$GET_LOADAV(loadav);
            reply_put_bytes(result, 0x08, loadav, sizeof(loadav));
        }
        break;

    case ASKNODE_REQ_PROC_WS_INFO: /* 0x4B, 0x00E6538C-0x00E6543A */
        /*
         * Four calls in a row, each abandoning the arm as soon as the shared
         * status word is non-zero.  Note that MMAP_$GET_WS_SIZ's three
         * results are read into registers BEFORE its status is tested
         * (0x00E65400-0x00E65410), which is what the ordering below keeps.
         */
        {
            uint16_t pid;
            uint16_t asid;
            uint16_t ws_index = 0;
            uint32_t size_a = 0;
            uint32_t size_b = 0;
            uint32_t size_c = 0;

            pid = PROC2_$GET_PID(param, local_status);
            if (*local_status != 0) {
                break;
            }
            asid = PROC2_$GET_ASID(param, local_status);
            if (*local_status != 0) {
                break;
            }
            MMAP_$GET_WS_INDEX(pid, &ws_index, local_status);
            if (*local_status != 0) {
                break;
            }
            MMAP_$GET_WS_SIZ(ws_index, &size_a, &size_b, &size_c,
                             local_status);
            if (*local_status != 0) {
                break;
            }
            reply_put_l(result, 0x14, size_a);
            reply_put_l(result, 0x10, size_b);
            reply_put_l(result, 0x18, size_c);
            MST_$GET_PRIVATE_SIZE(&asid,
                                  (uint32_t *)((uint8_t *)result + 0x08),
                                  (uint32_t *)((uint8_t *)result + 0x0C),
                                  local_status);
        }
        break;

    case ASKNODE_REQ_REV_INFO:    /* 0x4D, 0x00E6543E-0x00E65448 */
        OS_$GET_REV_INFO((uint8_t *)result + 0x08);
        break;

    case ASKNODE_REQ_ZOMBIE_LIST: /* 0x4F, 0x00E649C2-0x00E649E8 */
        /*
         * The scan bounds and the "more to come" flag are scratch here: only
         * the uid list at reply+0x0A and its count at reply+0x08 are
         * reported.  The start index is explicitly zeroed first
         * ("clr.l (-0x108,A6)" at 0x00E649C2).
         */
        {
            int32_t start_index = 0;
            uint8_t more_flag = 0;
            int32_t last_index = 0;

            PROC2_$ZOMBIE_LIST((uid_t *)((uint8_t *)result + 0x0A),
                               (uint16_t *)&asknode_$c_max_procs,
                               (uint16_t *)((uint8_t *)result + 0x08),
                               &start_index, &more_flag, &last_index);
            *local_status = 0;
        }
        break;

    case ASKNODE_REQ_DISK_INFO:   /* 0x51, 0x00E6484A-0x00E64862 */
        /*
         * One controller's statistics: the parameter block supplies the
         * controller number and the unit, and the "has statistics" byte goes
         * into the reply at +0x1E rather than into a frame cell.
         */
        DISK_$GET_STATS(0, (int16_t)param_get_w(param, 0x00),
                        (int16_t)param_get_w(param, 0x02),
                        (uint8_t *)result + 0x1E,
                        (uint8_t *)result + 0x08);
        break;

    case ASKNODE_REQ_DISPLAY_LIST: /* 0x55, 0x00E6544C-0x00E65500 */
        /*
         * One 18-byte record per display: the display type word, the display
         * uid and eight bytes of geometry.  Every SMD call is given the
         * REPLY's status field, not the local one ("pea (0x4,A1)" at
         * 0x00E6547A / 0x00E654A2).
         */
        {
            uint16_t n_devices = SMD_$N_DEVICES();
            uint16_t unit;
            uint16_t i;

            reply_put_w(result, 0x08, n_devices);
            reply_put_w(result, 0x0A, 0x12);

            unit = 1;
            for (i = 0; i < n_devices; i++) {
                unsigned entry = 0x0C + (unsigned)i * 0x12;
                uid_t disp_uid;
                smd_disp_info_result_t disp_info;
                uint16_t unit_cell;

                unit_cell = unit;
                SMD_$INQ_DISP_UID(&unit_cell, &disp_uid,
                                  (status_$t *)((uint8_t *)result + 0x04));
                reply_put_l(result, entry + 0x02, disp_uid.high);
                reply_put_l(result, entry + 0x06, disp_uid.low);

                unit_cell = unit;
                SMD_$INQ_DISP_INFO(&unit_cell, &disp_info,
                                   (status_$t *)((uint8_t *)result + 0x04));
                reply_put_w(result, entry + 0x00, disp_info.display_type);
                reply_put_bytes(result, entry + 0x0A,
                                (const uint8_t *)&disp_info + 2, 8);

                unit++;
            }

            /*
             * 0x00E654D4-0x00E65500: when more than one display was reported
             * but the first one's type is below 8, only the SECOND record is
             * kept - it is copied down over the first and the count is
             * forced to 1.
             */
            if (reply_get_w(result, 0x08) > 1 &&
                (int16_t)reply_get_w(result, 0x0C) < 8) {
                uint8_t entry1[0x12];
                reply_put_w(result, 0x08, 1);
                memcpy(entry1, (const uint8_t *)result + 0x1E, sizeof(entry1));
                reply_put_bytes(result, 0x0C, entry1, sizeof(entry1));
            }
        }
        break;

    case ASKNODE_REQ_PROC_LIST2:  /* 0x57, 0x00E649EC-0x00E64A08 */
        PROC2_$LIST2((uid_t *)((uint8_t *)result + 0x10),
                     (uint16_t *)&asknode_$c_max_procs2,
                     (uint16_t *)((uint8_t *)result + 0x08),
                     (int32_t *)param,
                     (uint8_t *)((uint8_t *)result + 0x0A),
                     (int32_t *)((uint8_t *)result + 0x0C));
        *local_status = 0;
        break;

    case ASKNODE_REQ_ZOMBIE_LIST2: /* 0x59, 0x00E64A0C-0x00E64A22 */
        /*
         * The same six arguments as request 0x57, but the branch at
         * 0x00E64A22 jumps into the request-0x4F arm's call site, so the
         * callee is PROC2_$ZOMBIE_LIST.
         */
        PROC2_$ZOMBIE_LIST((uid_t *)((uint8_t *)result + 0x10),
                           (uint16_t *)&asknode_$c_max_procs2,
                           (uint16_t *)((uint8_t *)result + 0x08),
                           (int32_t *)param,
                           (uint8_t *)((uint8_t *)result + 0x0A),
                           (int32_t *)((uint8_t *)result + 0x0C));
        *local_status = 0;
        break;

    case ASKNODE_REQ_TIME_SYNC:   /* 0x45 */
        /*
         * The cmpi chain has no 0x45 arm: it reaches the default at
         * 0x00E65502 like any other unknown code.  Kept as its own case so
         * that the WHO time-sync request the SERVER handles is visible here.
         */
        *status = status_$network_unknown_request_type;
        break;

    default:
        /*
         * 0x00E64786 falls through the whole cmpi chain to 0x00E65502:
         *   move.l #0x11000d,(A4)
         * A4 is the caller's status_ret (0x20,A6), not the local status word
         * at (-0x110,A6) - so *status carries the error and reply+0x04 stays
         * zero, since 0x00E65508 copies the untouched local status there.
         */
        *status = status_$network_unknown_request_type;
        break;
    }

    return ret_val;
}

/*
 * Main ASKNODE_$INTERNET_INFO function
 */
uint32_t ASKNODE_$INTERNET_INFO(uint16_t *req_type, uint32_t *node_id,
                                int32_t *req_len, uid_t *param,
                                uint16_t *resp_len, uint32_t *result,
                                status_$t *status)
{
    uint32_t target_node = *node_id;
    status_$t local_status = 0;
    uint32_t ret_val = 0;
    uint16_t request = *req_type;

    /*
     * Check if this is a local node query.
     * Local queries are handled directly without network communication.
     */
    if (target_node == NODE_$ME || target_node == 0) {
        /* Initialize status and set protocol version */
        *status = 0;
        result[1] = 0;  /* Clear status word in result */

        /* Set protocol version (2 or 3) */
        if (*(uint16_t *)result != 2) {
            *(uint16_t *)result = 3;
        }

        /* Handle the request locally */
        ret_val = handle_local_request(request, param, result, &local_status,
                                       status);

        /* Set response type (request + 1) */
        *(uint16_t *)((char *)result + 2) = request + 1;
        result[1] = local_status;

        return ret_val;
    }

    /*
     * Remote node query - check if network requests are enabled
     */
    if ((NETWORK_$CAPABLE_FLAGS & 1) == 0) {
        *status = status_$network_request_denied_by_local_node;
        return ret_val;
    }

    /*
     * Special case for request 0x1F (network diagnostics)
     * which handles retries differently
     */
    if (request == 0x1F) {
        int32_t routing = *req_len;
        int8_t retry_flag = 0;

        if (*(uint16_t *)result != 2) {
            *(uint16_t *)result = 3;
        }
        *(uint16_t *)((char *)result + 2) = request + 1;

        if (routing == -1) {
            routing = 0;
            retry_flag = 0;
        }

        /* Try NETWORK_$RING_INFO with retry on failure */
        do {
            NETWORK_$RING_INFO(&routing, (ring_info_t *)(result + 2), status);
            if (*status != status_$network_transmit_failed || *req_len != -1 || retry_flag < 0) {
                break;
            }
            /*
             *   00e655a6    move.l D4,-(SP)          ; node_id (pointer)
             *   00e655a8    pea (A2)                 ; A2 = &NAME_$ROOT_UID (0xE8029C)
             */
            routing = DIR_$FIND_NET(&NAME_$ROOT_UID, node_id);
            ret_val = 0;
            if (routing == 0) break;
            retry_flag = -1;
        } while (1);

        result[1] = *status;
        return ret_val;
    }

    /*
     * Standard remote query using PKT_$SAR_INTERNET
     */
    {
        /* Build request packet */
        uint16_t req_buf[12];   /* Request buffer (0x18 bytes) */
        uint32_t pkt_info[8];   /* A6-0xB8: the 30-byte PKT_$DEFAULT_INFO copy */
        /*
         * A6-0xD8: PKT_$SAR_INTERNET's tenth argument, a second packet-info
         * record of the same shape that the callee WRITES.  When a request
         * times out with no reply it stores the attempt counter there:
         * "movea.l (0x24,A6),A0 / move.w D3w,(0x8,A0)" at
         * 0x00E7205E-0x00E72062, just before setting status 0x00110007.
         * ASKNODE_$INTERNET_INFO never reads it back - the record only has
         * to exist so the callee's store lands in this frame and not
         * through a nil pointer.  (source-0fks)
         */
        uint32_t sar_resp_info[8];
        uint16_t resp_tpl_len;      /* A6-0x146 */
        /*
         * A6-0x142, PKT_$SAR_INTERNET's sixteenth argument: the length of the
         * reply DATA area the callee actually filled in.  The image copies it
         * into D2 right after each call (0x00E65748) and the request-0x31 arm
         * at 0x00E6589C-0x00E658A0 writes that copy back into the reply, so
         * the cell has to outlive the retry loop.
         */
        uint16_t resp_data_len = 0;
        uint16_t data_len = 0;
        int32_t port = *req_len;
        int8_t retry_flag = 0;

        /* Initialize request */
        req_buf[0] = 3;         /* Protocol version */
        req_buf[1] = request;   /* Request type */
        req_buf[2] = 0;         /* Reserved */

        /* Copy parameter based on request type */
        switch (request) {
        case 0x2B:
        case 0x23:
        case 0x14:
        case 0x4B:
            /* Copy 8-byte UID */
            *(uint32_t *)&req_buf[4] = param->high;
            *(uint32_t *)&req_buf[6] = param->low;
            break;

        case 0x16:
            /* Copy 12 bytes */
            {
                uint8_t *src = (uint8_t *)param;
                uint8_t *dst = (uint8_t *)&req_buf[4];
                int i;
                for (i = 0; i < 12; i++) *dst++ = *src++;
            }
            break;

        case 0x25:
            /* Copy 10 bytes */
            *(uint32_t *)&req_buf[4] = param->high;
            *(uint32_t *)&req_buf[6] = param->low;
            *(uint32_t *)&req_buf[8] = param[1].high;
            req_buf[10] = *(uint16_t *)&param[1].low;
            break;

        case 0x35:
            /* Copy 16 bytes */
            {
                uint8_t *src = (uint8_t *)param;
                uint8_t *dst = (uint8_t *)&req_buf[4];
                int i;
                for (i = 0; i < 16; i++) *dst++ = *src++;
            }
            break;

        case 0x5B:
        case 0x3D:
        case 0x3B:
            /* Copy high word and partial low */
            *(uint32_t *)&req_buf[4] = param->high;
            req_buf[6] = *(uint16_t *)&param->low;
            break;

        case 0x31:
            /* Log read request */
            *(uint32_t *)&req_buf[4] = param->high;
            if ((param->high & 0x10000) == 0) {
                data_len = *(uint16_t *)&param->high;
                if (data_len > 0x400) data_len = 0x400;
            } else {
                data_len = 0x400;
            }
            break;

        default:
            *(uint32_t *)&req_buf[4] = param->high;
            break;
        }

        /* If port is -1, use hint system to find routing */
        if (port == -1) {
            uid_t hint_uid;
            uint32_t hints[10];
            hint_uid.high = UID_$NIL.high;
            hint_uid.low = (UID_$NIL.low & 0xFFF00000) | *node_id;
            HINT_$GET_HINTS(&hint_uid, hints);
            port = hints[0];
            retry_flag = 0;
        }

        /* Copy packet info block */
        {
            uint32_t *src = PKT_$DEFAULT_INFO;
            uint32_t *dst = pkt_info;
            int i;
            for (i = 0; i < 7; i++) *dst++ = *src++;
            *(uint16_t *)dst = *(uint16_t *)src;
        }

        /* Send and receive */
        do {
            /*
             * 0x00E656FC - 0x00E6573C, seventeen arguments and a 0x38-byte
             * caller cleanup.  Pushed last to first:
             *    1  (-0xE8,A6)          the routing key
             *    2  *node_id            "movea.l D4,A0 / move.l (A0),-(SP)"
             *    3  #4                  the ASKNODE socket
             *    4  &(-0xB8,A6)         the PKT_$DEFAULT_INFO copy
             *    5  #6                  timeout
             *    6  &(-0x100,A6)        the request template
             *    7  #0x18               its length
             *    8  (0x1AE,PC)          the empty request-data cell at
             *                           0x00E658CC
             *    9  #0                  request data length
             *   10  &(-0xD8,A6)         sar_resp_info, see above
             *   11  (0x1C,A6)           the caller's reply record
             *   12  *(D7)               its capacity
             *   13  &(-0x146,A6)        resp_tpl_len
             *   14  (0x1C,A6) + 0x0A    the reply data area
             *   15  (-0x144,A6)         its capacity
             *   16  &(-0x142,A6)        resp_data_len
             *   17  A4                  status_ret
             */
            PKT_$SAR_INTERNET(port, *node_id, 4, pkt_info, 6,
                              req_buf, 0x18,
                              &ASKNODE_$EMPTY_DATA, 0,  /* No request data */
                              sar_resp_info, (char *)result, *resp_len,
                              &resp_tpl_len, (uint16_t *)((char *)result + 10),
                              data_len,
                              &resp_data_len, status);

            if ((*status != status_$network_transmit_failed &&
                 *status != status_$network_remote_node_failed_to_respond) ||
                *req_len != -1 || retry_flag < 0) {
                break;
            }

            /*
             *   00e6576a    move.l D4,-(SP)          ; node_id (pointer)
             *   00e6576c    pea (A3)                 ; A3 = &NAME_$ROOT_UID (0xE8029C)
             */
            port = DIR_$FIND_NET(&NAME_$ROOT_UID, node_id);
            if (port == 0) break;
            retry_flag = -1;
        } while (1);

        /* Check response */
        if (*status != 0) {
            /* Set high bit to indicate remote error */
            *(uint8_t *)status |= 0x80;
            return *status;
        }

        /*
         * Validate response type (0x00E65798 - 0x00E657AA).  The constant the
         * original stores is 0x11000B, "unexpected reply type"; the response
         * type is the word at +0x02 of the reply record.
         */
        if ((uint32_t)*(uint16_t *)((char *)result + 2) != (uint32_t)request + 1) {
            *status = status_$network_unexpected_reply_type;
            return (uint16_t)*result;
        }

        /* Validate protocol version (0x00E657AE - 0x00E657CA): 0x110015 */
        if (*(uint16_t *)result != 3 && *(uint16_t *)result != 2 && ASKNODE_$PROTOCOL_VERSION != 3) {
            *status = status_$network_bad_asknode_version_number;
            return (uint16_t)*result;
        }

        /* Check for remote error status */
        if (result[1] != 0) {
            *status = result[1];
        }

        /* Handle hint updates for certain request types */
        if (result[1] == 0) {
            if (request == 0x0A || request == 0x04 || request == 0x18) {
                /* Update hints based on response - extract UID from result */
                uid_t response_uid;
                response_uid.high = result[2];
                response_uid.low = result[3];
                HINT_$ADDI(&response_uid, (uint32_t *)&port);
            }
        }

        /*
         * Special handling for the log-read response (0x00E65894-0x00E658A0):
         *   cmpi.w #0x31,(-0xfe,A6) / movea.l (0x1c,A6),A1
         *   move.w D2w,(0x8,A1)
         * D2 is the reply DATA length PKT_$SAR_INTERNET returned
         * (0x00E65748), not the request-side clamp in (-0x144,A6).
         */
        if (request == 0x31) {
            *(uint16_t *)(result + 2) = resp_data_len;
        }
    }

    return ret_val;
}
