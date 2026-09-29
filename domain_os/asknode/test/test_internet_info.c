/*
 * asknode/test/test_internet_info.c - unit tests for ASKNODE_$INTERNET_INFO
 *                                     (0x00E645EA)
 *
 * These tests #include asknode/internet_info.c and drive the real function
 * through mocked callees, so what is exercised is the emitted translation and
 * not a re-implementation.
 *
 * What they pin down (bead source-1b7z):
 *
 *   - the memory-statistics copy at 0x00E64812-0x00E6482A moves the WHOLE
 *     0x56-byte MEM_$MEM_REC (21 longwords + one word) to reply+0x72, not the
 *     21 words the tree used to copy, so the per-page parity-error table
 *     reaches the reply;
 *   - the MMAP_$REAL_PAGES word at reply+0x76 (0x00E6483A / 0x00E64842) is
 *     stored AFTER that copy and therefore overwrites two of its bytes;
 *   - the negated-uid store at reply+0x12 (0x00E648CC-0x00E648D2) that the
 *     node-uid and root-uid arms make;
 *   - the unknown-request arm at 0x00E65502 writes the caller's status_ret,
 *     leaving reply+0x04 zero.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

/* ==========================================================================
 * Test infrastructure
 * ========================================================================== */

static int tests_passed = 0;
static int tests_failed = 0;
static int current_failed = 0;
static void reset_mocks(void);

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do {                                                   \
    printf("  Running %s... ", #name);                                        \
    current_failed = 0;                                                       \
    reset_mocks();                                                            \
    test_##name();                                                            \
    if (current_failed == 0) { tests_passed++; printf("PASSED\n"); }          \
} while (0)

#define ASSERT_EQ(expected, actual) do {                                      \
    if ((unsigned long long)(expected) != (unsigned long long)(actual)) {     \
        printf("FAILED\n    Expected: 0x%llx, Got: 0x%llx at line %d\n",      \
               (unsigned long long)(expected),                                \
               (unsigned long long)(actual), __LINE__);                       \
        tests_failed++; current_failed = 1;                                   \
        return;                                                               \
    }                                                                         \
} while (0)

#define ASSERT_TRUE(cond) do {                                                \
    if (!(cond)) {                                                            \
        printf("FAILED\n    Assertion failed at line %d: %s\n",               \
               __LINE__, #cond);                                              \
        tests_failed++; current_failed = 1;                                   \
        return;                                                               \
    }                                                                         \
} while (0)

/* ==========================================================================
 * Headers the translation unit under test needs
 * ========================================================================== */

#include "asknode/asknode_internal.h"

/* ==========================================================================
 * Kernel data the function reads
 * ========================================================================== */

uint32_t NODE_$ME;
uint32_t TIME_$BOOT_TIME;
uint32_t TIME_$CURRENT_CLOCKH;
uint32_t ROUTE_$PORT;
mmap_globals_t MMAP_GLOBALS_STORAGE;    /* MMAP_$REAL_PAGES lives in here */
mem_data_t MEM_DATA;
MODULE_DATA_DEFINE(ring_global_t, RING_$CTL, 0x00E86400);
MODULE_DATA_DEFINE(ring_$wired_data_t, RING_$WIRED_DATA, 0x00E261AC);  /* stats[0]: the 15-longword copy source */
cal_$timezone_rec_t CAL_$TIMEZONE;
name_$data_t NAME_$DATA;                /* NAME_$ROOT_UID lives in here */
uid_t UID_$NIL;
uint32_t NETWORK_$MOTHER_NODE;
uint32_t NETWORK_$ALLOWED_SERVICE;
uid_t NETWORK_$PAGING_FILE_UID;
int8_t NETWORK_$DISKLESS;
uint16_t NETWORK_$INFO_RQST_CNT;
uint16_t NETWORK_$MULT_PAGIN_RQST_CNT;
uint16_t NETWORK_$PAGIN_RQST_CNT;
uint16_t NETWORK_$PAGOUT_RQST_CNT;
uint16_t NETWORK_$READ_CALL_CNT;
uint16_t NETWORK_$WRITE_CALL_CNT;
uint16_t NETWORK_$READ_VIOL_CNT;
uint16_t NETWORK_$WRITE_VIOL_CNT;
uint16_t NETWORK_$BAD_CHKSUM_CNT;
uint32_t ASKNODE_$EMPTY_DATA;
MODULE_DATA_DEFINE(asknode_$data_t, ASKNODE_$DATA, 0x00E82408);

/* Data the arms recovered in bead source-yjtx read. */
uint32_t NETWORK_$PAGING_BACKLOG[NETWORK_PAGING_BACKLOG_BUCKETS];
uint32_t NETWORK_$FILE_BACKLOG[NETWORK_FILE_BACKLOG_BUCKETS];
network_$failure_rec_t NETWORK_$FAILURE_REC;
int8_t   NETWORK_$ACTIVITY_FLAG;
uint16_t NETWORK_$RCV_READ_AHEAD;
uint16_t NETWORK_$SET_ATTRIB_CALL_CNT;
uint16_t NETWORK_$ATTRIB_RQST_CNT;
uint16_t NETWORK_$2LONG1;
uint16_t REM_FILE_$2LONG1;

uint16_t RING_$FILE_OVERFLOW;
uint16_t RING_$OVERFLOW_OVERFLOW;
uint16_t RING_$DELIVERY_FAILED;

route_$port_t  route_ports[8];
MODULE_DATA_DEFINE(route_$wired_data_t, ROUTE_$WIRED_DATA, 0x00E26EE4);
MODULE_DATA_DEFINE(route_$unwired_data_t, ROUTE_$UNWIRED_DATA, 0x00E825DC);
MODULE_DATA_DEFINE(route_$rtwired_data_t, ROUTE_$RTWIRED_DATA, 0x00E87D80);

MODULE_DATA_DEFINE(rip_$wired_data_t, RIP_$WIRED_DATA, 0x00E26258);

uint32_t MMU_$SYSTEM_REV;
int8_t   GPU_$PRESENT;
uint32_t PROM_$MACHINE_ID;
os_$boot_device_t OS_$BOOT_DEVICE;
int16_t  CAL_$BOOT_VOLX;

/* ==========================================================================
 * Mock state
 * ========================================================================== */

/*
 * The reply record.  0xC8 bytes is exactly the end of the memory-statistics
 * copy (0x72 + 0x56); the array is longer so an over-copy is visible, and it
 * is a uint32_t array so the record is aligned the way a kernel reply is.
 */
#define REPLY_LONGS 0x40
static uint32_t reply[REPLY_LONGS];
static uint8_t  *reply_bytes = (uint8_t *)reply;

static uid_t     mock_node_uid;
static uid_t     mock_root_uid;
static int       mock_disk_get_stats_calls;
static uid_t     mock_node_data_uid;
static int16_t   mock_disk_ctype;
static int16_t   mock_disk_cnum;
static int16_t   mock_disk_unit;
static int       mock_signal_os_calls;
static int       mock_signal_pgroup_calls;
static int16_t   mock_signal_number;
static uint16_t  mock_ringlog_cmd;
static int       mock_ringlog_calls;
static int       mock_netlog_calls;
static int16_t   mock_netlog_cmd;
static uint32_t  mock_netlog_kinds;
static status_$t mock_ringlog_status;
static int16_t   mock_find_port_result;
static route_$port_t *mock_short_port_arg;
static int       mock_short_port_calls;
static uint16_t  mock_proc_info_len;
static int16_t   mock_proc_info_scan_key;
static int16_t   mock_proc_info_pid;
static int       mock_proc_info_calls;

static void reset_mocks(void)
{
    memset(reply, 0, sizeof(reply));
    memset(&MEM_DATA, 0, sizeof(MEM_DATA));
    memset(&RING_$CTL, 0, sizeof(RING_$CTL));
    memset(RING_$WIRED_DATA.stats, 0, sizeof(RING_$WIRED_DATA.stats));
    memset(&CAL_$TIMEZONE, 0, sizeof(CAL_$TIMEZONE));
    memset(&MMAP_GLOBALS_STORAGE, 0, sizeof(MMAP_GLOBALS_STORAGE));
    NODE_$ME = 0x00012345;
    TIME_$BOOT_TIME = 0;
    TIME_$CURRENT_CLOCKH = 0;
    ROUTE_$PORT = 0;
    MMAP_$REAL_PAGES = 0;
    NETWORK_$DISKLESS = 0;
    NETWORK_$MOTHER_NODE = 0;
    mock_node_uid.high = 0;
    mock_node_uid.low = 0;
    mock_root_uid.high = 0;
    mock_root_uid.low = 0;
    mock_node_data_uid.high = 0;
    mock_node_data_uid.low = 0;
    mock_disk_get_stats_calls = 0;
    mock_disk_ctype = -1;
    mock_disk_cnum = -1;
    mock_disk_unit = -1;
    mock_signal_os_calls = 0;
    mock_signal_pgroup_calls = 0;
    mock_signal_number = 0;
    mock_ringlog_cmd = 0;
    mock_ringlog_calls = 0;
    mock_netlog_calls = 0;
    mock_netlog_cmd = 0;
    mock_netlog_kinds = 0;
    mock_ringlog_status = 0;
    mock_find_port_result = -1;
    mock_short_port_arg = (route_$port_t *)0;
    mock_short_port_calls = 0;
    mock_proc_info_len = 0;
    mock_proc_info_scan_key = -1;
    mock_proc_info_pid = -1;
    mock_proc_info_calls = 0;

    memset(NETWORK_$PAGING_BACKLOG, 0, sizeof(NETWORK_$PAGING_BACKLOG));
    memset(NETWORK_$FILE_BACKLOG, 0, sizeof(NETWORK_$FILE_BACKLOG));
    memset(&NETWORK_$FAILURE_REC, 0, sizeof(NETWORK_$FAILURE_REC));
    memset(&RING_$WIRED_DATA.swdiag, 0, sizeof(RING_$WIRED_DATA.swdiag));
    memset(ROUTE_$RTWIRED_DATA.q_depth, 0, sizeof(ROUTE_$RTWIRED_DATA.q_depth));
    memset(&RIP_$WIRED_DATA, 0, sizeof(RIP_$WIRED_DATA));
    memset(&RIP_$WIRED_DATA.stats, 0, sizeof(RIP_$WIRED_DATA.stats));
    memset(route_ports, 0, sizeof(route_ports));
    {
        int i;
        for (i = 0; i < 8; i++) {
            ROUTE_$WIRED_DATA.portp[i] = &route_ports[i];
        }
    }
    NETWORK_$ACTIVITY_FLAG = 0;
    ROUTE_$RTWIRED_DATA.netbuf_alloc = 0;
    ROUTE_$RTWIRED_DATA.q_oflo = 0;
    ROUTE_$WIRED_DATA.n_routing_ports = 0;
    ROUTE_$WIRED_DATA.std_n_routing_ports = 0;
    ROUTE_$UNWIRED_DATA.start_time = 0;
}

/* ==========================================================================
 * Mocked callees
 * ========================================================================== */

void NAME_$GET_NODE_UID(uid_t *node_uid) { *node_uid = mock_node_uid; }
void NAME_$GET_ROOT_UID(uid_t *root_uid) { *root_uid = mock_root_uid; }

void NAME_$GET_NODE_DATA_UID(uid_t *uid) { *uid = mock_node_data_uid; }

void DISK_$GET_STATS(int16_t ctype, int16_t cnum, int16_t unit,
                     uint8_t *has_stats, void *stats)
{
    mock_disk_ctype = ctype;
    mock_disk_cnum = cnum;
    mock_disk_unit = unit;
    /* The real routine always preloads the buffer; make each unit's bytes
     * distinguishable so the request-0x10 packing can be checked. */
    memset(stats, (uint8_t)(0x10 + unit), DISK_STATS_SIZE);
    mock_disk_get_stats_calls++;
    *has_stats = 0;
}

void VOLX_$GET_INFO(int16_t *vol_idx, uid_t *dir_uid_ret, uint32_t *free_blocks,
                    uint32_t *total_blocks, status_$t *status_ret)
{
    (void)vol_idx; (void)dir_uid_ret; (void)free_blocks; (void)total_blocks;
    *status_ret = 0;
}

void PROC2_$LIST(uid_t *uid_list, uint16_t *max_count, uint16_t *count)
{ (void)uid_list; (void)max_count; *count = 0; }

void PROC2_$GET_INFO(uid_t *proc_uid, void *info, uint16_t *info_len,
                     status_$t *status_ret)
{ (void)proc_uid; (void)info; (void)info_len; *status_ret = 0; }

void PROC2_$SIGNAL_PGROUP_OS(uid_t *pgroup_uid, int16_t *signal,
                             uint32_t *flags, status_$t *status_ret)
{
    (void)pgroup_uid; (void)flags;
    mock_signal_pgroup_calls++;
    mock_signal_number = *signal;
    *status_ret = 0;
}

void GET_BUILD_TIME(char *buf, int16_t *len_p) { (void)buf; *len_p = 0; }

void LOG_$READ(void *buffer, uint16_t *max_len, uint16_t *actual_len)
{ (void)buffer; (void)max_len; *actual_len = 0; }

void LOG_$READ2(void *buffer, uint16_t offset, uint16_t max_len,
                uint16_t *actual_len)
{ (void)buffer; (void)offset; (void)max_len; *actual_len = 0; }

void NETWORK_$RING_INFO(void *net_handle, ring_info_t *ring_info,
                        status_$t *status_ret)
{ (void)net_handle; (void)ring_info; *status_ret = 0; }

uint32_t DIR_$FIND_NET(uid_t *dir_uid, uint32_t *index)
{ (void)dir_uid; (void)index; return 0; }

int16_t HINT_$GET_HINTS(uid_t *file_uid, uint32_t *addresses)
{ (void)file_uid; addresses[0] = 0; return 0; }

void HINT_$ADDI(uid_t *uid_ptr, uint32_t *addresses)
{ (void)uid_ptr; (void)addresses; }

void PKT_$SAR_INTERNET(uint32_t routing_key, uint32_t dest_node, uint16_t dest_sock,
                       void *pkt_info, int16_t timeout,
                       void *req_template, uint16_t req_tpl_len,
                       void *req_data, uint16_t req_data_len,
                       pkt_$sar_result_t *resp_buf,
                       char *resp_tpl_buf, uint16_t resp_tpl_max,
                       uint16_t *resp_tpl_len, void *resp_data_buf,
                       uint16_t resp_data_max, uint16_t *resp_data_len,
                       status_$t *status_ret)
{
    (void)routing_key; (void)dest_node; (void)dest_sock; (void)pkt_info;
    (void)timeout; (void)req_template; (void)req_tpl_len; (void)req_data;
    (void)req_data_len; (void)resp_buf; (void)resp_tpl_buf; (void)resp_tpl_max;
    (void)resp_data_buf; (void)resp_data_max;
    *resp_tpl_len = 0;
    *resp_data_len = 0;
    *status_ret = 0;
}

/* ---- callees the arms recovered in bead source-yjtx reach ---------------- */

void PROC2_$LIST2(uid_t *uid_list, uint16_t *max_count, uint16_t *count,
                  int32_t *start_index, int8_t *more_flag,
                  int32_t *last_index)
{
    (void)uid_list; (void)start_index; (void)more_flag; (void)last_index;
    *count = *max_count;
}

void PROC2_$ZOMBIE_LIST(uid_t *uid_list, uint16_t *max_count, uint16_t *count,
                        int32_t *start_index, int8_t *more_flag,
                        int32_t *last_index)
{
    (void)uid_list; (void)start_index; (void)more_flag; (void)last_index;
    *count = *max_count;
}

void PROC2_$INFO(int16_t *scan_key, int16_t *pid, void *info,
                 uint16_t *info_len, status_$t *status_ret)
{
    (void)info;
    mock_proc_info_calls++;
    mock_proc_info_scan_key = *scan_key;
    mock_proc_info_pid = *pid;
    mock_proc_info_len = *info_len;
    *status_ret = 0;
}

void PROC2_$GET_UPIDS(uid_t *proc_uid, uint16_t *upid, uint16_t *upgid,
                      uint16_t *uppid, status_$t *status_ret)
{
    (void)proc_uid;
    *upid = 0x1111; *upgid = 0x2222; *uppid = 0x3333;
    *status_ret = 0;
}

uint16_t PROC2_$GET_PID(uid_t *proc_uid, status_$t *status_ret)
{ (void)proc_uid; *status_ret = 0; return 0x0042; }

uint16_t PROC2_$GET_ASID(uid_t *proc_uid, status_$t *status_ret)
{ (void)proc_uid; *status_ret = 0; return 0x0007; }

void PROC2_$SIGNAL_OS(uid_t *proc_uid, int16_t *signal, uint32_t *param,
                      status_$t *status_ret)
{
    (void)proc_uid; (void)param;
    mock_signal_os_calls++;
    mock_signal_number = *signal;
    *status_ret = 0;
}

void PROC1_$GET_LIST(int16_t *count_ret, proc_list_entry_t *list_ret)
{ (void)list_ret; *count_ret = 0; }

void PROC1_$GET_LOADAV(uint32_t *loadav)
{ loadav[0] = 0x11111111u; loadav[1] = 0x22222222u; loadav[2] = 0x33333333u; }

void RINGLOG_$CNTL(uint16_t *cmd_ptr, void *param, status_$t *status_ret)
{
    (void)param;
    mock_ringlog_calls++;
    mock_ringlog_cmd = *cmd_ptr;
    *status_ret = mock_ringlog_status;
}

void NETLOG_$CNTL(int16_t *cmd, uint32_t *node, uint16_t *sock,
                  uint32_t *kinds, status_$t *status_ret)
{
    (void)node; (void)sock;
    mock_netlog_calls++;
    mock_netlog_cmd = *cmd;
    mock_netlog_kinds = *kinds;
    *status_ret = 0;
}

void IO_$GET_CONFIG(uint16_t *c1, uint16_t *c2, uint16_t *c3, uint16_t *c4)
{ *c1 = 1; *c2 = 2; *c3 = 3; *c4 = 4; }

uint16_t SMD_$N_DEVICES(void) { return 0; }

void SMD_$INQ_DISP_UID(uint16_t *unit, uid_t *uid, status_$t *status_ret)
{ (void)unit; uid->high = 0; uid->low = 0; *status_ret = 0; }

void SMD_$INQ_DISP_INFO(uint16_t *unit, smd_disp_info_result_t *info,
                        status_$t *status_ret)
{ (void)unit; memset(info, 0, sizeof(*info)); *status_ret = 0; }

void PEB_$GET_INFO(uint16_t *info_flags, uint8_t *info_byte)
{ *info_flags = 0; *info_byte = 0; }

void DISK_$GET_MNT_INFO(uint16_t *vol_idx_ptr, void *param_2, void *info,
                        status_$t *status)
{
    (void)vol_idx_ptr; (void)param_2;
    memset(info, 0, 0x2A);
    *status = 0;
}

int16_t ROUTE_$FIND_PORT(uint16_t network, int32_t socket)
{ (void)network; (void)socket; return mock_find_port_result; }

void ROUTE_$SHORT_PORT(route_$port_t *port_struct, route_$short_port_t *out)
{
    (void)out;
    mock_short_port_calls++;
    mock_short_port_arg = port_struct;
}

void NET_IO_$DEVICE_STAT(uint16_t network, uint16_t index, uint16_t max_len,
                         void *id_ret, void *stat_buf, uint16_t *stat_len_ret,
                         status_$t *status_ret)
{
    (void)network; (void)index; (void)max_len; (void)stat_buf;
    memset(id_ret, 0, 8);
    *stat_len_ret = 0;
    *status_ret = 0;
}

void NET_IO_$DEVICE_STAT2(uint16_t network, uint16_t index, uint16_t max_len,
                          void *id_ret, void *stat_buf, uint16_t *stat_len_ret,
                          status_$t *status_ret)
{
    NET_IO_$DEVICE_STAT(network, index, max_len, id_ret, stat_buf,
                        stat_len_ret, status_ret);
}

void MMAP_$GET_WS_INDEX(uint16_t pid, uint16_t *wsl_index, status_$t *status)
{ (void)pid; *wsl_index = 1; *status = 0; }

void MMAP_$GET_WS_SIZ(uint16_t wsl_index, uint32_t *page_count,
                      uint32_t *field_40, uint32_t *max_pages,
                      status_$t *status)
{
    (void)wsl_index;
    *page_count = 0xAAAAAAAAu;
    *field_40   = 0xBBBBBBBBu;
    *max_pages  = 0xCCCCCCCCu;
    *status = 0;
}

void MST_$GET_PRIVATE_SIZE(uint16_t *asid_p, uint32_t *size_ret,
                           uint32_t *size2_ret, status_$t *status_ret)
{ (void)asid_p; *size_ret = 0; *size2_ret = 0; *status_ret = 0; }

void OS_$GET_REV_INFO(void *buf) { memset(buf, 0, 12); }

/* ==========================================================================
 * The translation unit under test
 * ========================================================================== */

#include "../internet_info.c"

/* ==========================================================================
 * Helpers
 * ========================================================================== */

/* Drive a local-node query (node_id == 0 takes the local arm at 0x00E64622). */
static status_$t run_local(uint16_t req, uid_t *param)
{
    uint32_t node_id = 0;
    int32_t  req_len = 0;
    uint16_t resp_len = sizeof(reply);
    status_$t status = 0xdeadbeef;
    uid_t     zero_param = { 0, 0 };

    ASKNODE_$INTERNET_INFO(&req, &node_id, &req_len,
                           param ? param : &zero_param,
                           &resp_len, reply, &status);
    return status;
}

/*
 * Read a reply word the way the emitted code stores it.  The kernel is
 * big-endian but these tests run on the host, so words are compared as
 * values rather than as byte pairs (the record copy above is byte-exact
 * either way, being a straight block move).
 */
static uint16_t reply_word_at(unsigned off)
{
    uint16_t v;
    memcpy(&v, reply_bytes + off, sizeof(v));
    return v;
}

/* Fill MEM_$MEM_REC with a byte pattern that makes every offset distinct. */
static void fill_mem_rec(void)
{
    uint8_t *p = (uint8_t *)&MEM_$MEM_REC;
    unsigned i;
    for (i = 0; i < sizeof(mem_$mem_rec_t); i++) {
        p[i] = (uint8_t)(0xA0 + i);
    }
}

/* ==========================================================================
 * Tests
 * ========================================================================== */

/*
 * mem/mem.h models MEM_$MEM_REC as the 0x56 bytes 0xE22934..0xE22989, which
 * is what the loop at 0x00E6481C-0x00E6482A moves (21 longwords + one word).
 */
TEST(mem_rec_is_the_0x56_bytes_the_loop_moves)
{
    ASSERT_EQ(0x56, sizeof(mem_$mem_rec_t));
    ASSERT_EQ(21 * 4 + 2, sizeof(mem_$mem_rec_t));
    /* The copy lands at reply+0x72 and ends at 0xC8, inside the buffer. */
    ASSERT_TRUE(0x72 + sizeof(mem_$mem_rec_t) <= sizeof(reply));
}

/*
 * 0x00E64812-0x00E6482A: the whole record reaches reply+0x72.  Only the two
 * bytes at +0x76 differ, because the MMAP_$REAL_PAGES store at 0x00E6483A
 * runs after the copy.
 */
TEST(stats_copies_whole_mem_rec_to_reply_0x72)
{
    unsigned i;

    fill_mem_rec();
    MMAP_$REAL_PAGES = 0;           /* stores 0 over +0x76, see next test */
    memset(reply_bytes, 0x5A, sizeof(reply));

    run_local(0x06, NULL);

    for (i = 0; i < sizeof(mem_$mem_rec_t); i++) {
        uint8_t expect = (uint8_t)(0xA0 + i);
        if (0x72 + i == 0x76 || 0x72 + i == 0x77) {
            continue;               /* overwritten by MMAP_$REAL_PAGES */
        }
        if (reply_bytes[0x72 + i] != expect) {
            printf("FAILED\n    reply[0x%02x]: expected 0x%02x, got 0x%02x\n",
                   0x72 + i, expect, reply_bytes[0x72 + i]);
            tests_failed++; current_failed = 1;
            return;
        }
    }
}

/*
 * The copy is exactly 0x56 bytes: it does not reach back over the byte below
 * it, and it does not run past its end.  Byte 0x71 belongs to the 22-byte
 * disk-statistics block the same arm writes at reply+0x5C (0x00E647FC), so it
 * carries the mock's fill (0x10 for unit 0) rather than the 0x5A background -
 * seeing 0x5A there would mean the disk call had been dropped, and seeing a
 * memory-record byte would mean the copy started too low.
 */
TEST(stats_mem_rec_copy_does_not_overrun)
{
    fill_mem_rec();
    memset(reply_bytes, 0x5A, sizeof(reply));

    run_local(0x06, NULL);

    ASSERT_EQ(0x10, reply_bytes[0x5C]);          /* first disk-stats byte */
    ASSERT_EQ(0x10, reply_bytes[0x71]);          /* last disk-stats byte */
    ASSERT_EQ(0x5A, reply_bytes[0x72 + 0x56]);
    ASSERT_EQ(0x5A, reply_bytes[0x72 + 0x57]);
}

/*
 * The tail of the record is MEM_$PAGE_ERRORS (mem/mem.h): four 18-byte
 * per-page parity-error records at record offset 0x0E, i.e. reply+0x80.
 * These are the bytes the 21-word copy used to drop entirely.
 */
TEST(stats_reply_carries_the_page_error_table)
{
    unsigned i;

    memset(&MEM_DATA, 0, sizeof(MEM_DATA));
    for (i = 0; i < MEM_PAGE_ERROR_RECORDS; i++) {
        MEM_$PAGE_ERRORS[i].phys_addr = 0x00110000u * (i + 1);
        MEM_$PAGE_ERRORS[i].count = (uint16_t)(i + 1);
    }
    memset(reply_bytes, 0x5A, sizeof(reply));

    run_local(0x06, NULL);

    for (i = 0; i < MEM_PAGE_ERROR_RECORDS; i++) {
        const mem_$page_error_t *out =
            (const mem_$page_error_t *)(reply_bytes + 0x72 + 0x0E + i * 0x12);
        uint32_t phys;
        uint16_t count;
        memcpy(&phys, &out->phys_addr, sizeof(phys));
        memcpy(&count, &out->count, sizeof(count));
        ASSERT_EQ(0x00110000u * (i + 1), phys);
        ASSERT_EQ(i + 1, count);
    }
}

/*
 * 0x00E6482C-0x00E64846: the real-page count is stored at reply+0x76 after
 * the record copy, and is clamped to zero above 0xFFFF.
 */
TEST(stats_real_pages_word_follows_the_copy)
{
    fill_mem_rec();
    MMAP_$REAL_PAGES = 0x1234;
    run_local(0x06, NULL);
    /* the record byte the copy put here (0xA0 + 4) is gone */
    ASSERT_EQ(0x1234, reply_word_at(0x76));

    reset_mocks();
    fill_mem_rec();
    MMAP_$REAL_PAGES = 0x10000;
    run_local(0x06, NULL);
    ASSERT_EQ(0x0000, reply_word_at(0x76));
}

/*
 * 0x00E648CC-0x00E648D2 and 0x00E64900-0x00E64906: the node-uid arm writes
 * -(uid.high) at reply+0x12 and 1-(uid.high) at reply+0x22, the two
 * complements the remote arm checks at 0x00E6581C and 0x00E657F6.
 */
TEST(node_uid_reply_carries_the_uid_complements)
{
    uint32_t at_12, at_22;

    mock_node_uid.high = 0x11223344;
    mock_node_uid.low  = 0x55667788;

    run_local(0x04, NULL);

    ASSERT_EQ(0x11223344u, reply[2]);
    ASSERT_EQ(0x55667788u, reply[3]);
    memcpy(&at_12, reply_bytes + 0x12, 4);
    memcpy(&at_22, reply_bytes + 0x22, 4);
    ASSERT_EQ((uint32_t)-0x11223344, at_12);
    ASSERT_EQ((uint32_t)(1 - 0x11223344), at_22);
    /* the two checks the remote arm makes */
    ASSERT_EQ(0u, at_12 + reply[2]);
    ASSERT_EQ(1u, at_22 + reply[2]);
}

/*
 * 0x00E64786 -> 0x00E65502: an unrecognised request type puts 0x0011000D in
 * the caller's status_ret, and reply+0x04 keeps the (zero) local status that
 * 0x00E65516 copies there.
 */
TEST(unknown_request_writes_status_ret_not_the_reply_status)
{
    status_$t status;

    status = run_local(0x07, NULL);

    ASSERT_EQ(status_$network_unknown_request_type, status);
    ASSERT_EQ(0u, reply[1]);
    /* 0x00E65508-0x00E65512: the reply still gets request+1 at +0x02 */
    ASSERT_EQ(0x08, reply_word_at(0x02));
}

/*
 * The Pascal by-reference constant pool at 0x00E658AE..0x00E658CD:
 *   00e658ae  00 39 00 00 00 00 ff ff  ff ff 00 00 00 2a 00 02
 *   00e658be  00 01 00 04 00 03 00 00  01 f8 00 13 00 3e 00 00
 * Each cell is a file-static in internet_info.c; this test is what pins the
 * emitted values to that dump.
 */
TEST(constant_cells_match_the_code_segment_pool)
{
    ASSERT_EQ(0x0039, asknode_$c_max_procs);              /* 0x00E658AE */
    ASSERT_EQ(0x00000000u, asknode_$c_local_route_nexthop);    /* 0x00E658B0 */
    ASSERT_EQ(0xFFFFFFFFu, asknode_$c_local_route_expiration); /* 0x00E658B4 */
    ASSERT_EQ(0x0000, asknode_$c_local_route_metric);     /* 0x00E658B8 */
    ASSERT_EQ(0x002A, asknode_$c_mnt_info_size);          /* 0x00E658BA */
    ASSERT_EQ(0x0002, asknode_$c_disp_unit_2);            /* 0x00E658BC */
    ASSERT_EQ(0x0001, asknode_$c_disp_unit_1);            /* 0x00E658BE */
    ASSERT_EQ(0x0004, asknode_$c_ringlog_stop);           /* 0x00E658C0 */
    ASSERT_EQ(0x0003, asknode_$c_ringlog_clear);          /* 0x00E658C2 */
    ASSERT_EQ(0x0000, asknode_$c_zero);                   /* 0x00E658C4 */
    ASSERT_EQ(0x01F8, asknode_$c_proc_info_len);          /* 0x00E658C6 */
    ASSERT_EQ(0x0013, asknode_$c_signal);                 /* 0x00E658C8 */
    ASSERT_EQ(0x003E, asknode_$c_max_procs2);             /* 0x00E658CA */
}

/*
 * 0x00E64FC6-0x00E64FCE: the whole arm is one longword move.  Before bead
 * source-yjtx this request fell through to the default arm.
 */
TEST(request_0x37_returns_the_route_port)
{
    status_$t status;

    ROUTE_$PORT = 0x0BADF00Du;
    status = run_local(0x37, NULL);

    ASSERT_EQ(0u, status);
    ASSERT_EQ(0x0BADF00Du, reply[2]);
    ASSERT_EQ(0x38, reply_word_at(0x02));   /* request + 1 */
}

/*
 * 0x00E64F1E-0x00E64F38 + 0x00E65382: the 16-byte failure record reaches
 * reply+0x08, and the "failure recorded" flag at record+0x02 is cleared
 * first when the network is active ("clr.b (0x00E24BF6).l" at 0x00E64F26).
 */
TEST(request_0x2f_copies_the_failure_record_and_clears_the_flag)
{
    NETWORK_$FAILURE_REC.word0        = 0x1234;
    NETWORK_$FAILURE_REC.flag         = (int8_t)0xFF;
    NETWORK_$FAILURE_REC.byte3        = 0x77;
    NETWORK_$FAILURE_REC.node_id      = 0xAABBCCDDu;
    NETWORK_$FAILURE_REC.timestamp    = 0x01020304u;
    NETWORK_$FAILURE_REC.failure_type = 3;

    NETWORK_$ACTIVITY_FLAG = 0;             /* idle: the flag survives */
    run_local(0x2F, NULL);
    ASSERT_EQ((uint8_t)0xFF, reply_bytes[0x0A]);
    ASSERT_EQ((int8_t)0xFF, NETWORK_$FAILURE_REC.flag);

    reset_mocks();
    NETWORK_$FAILURE_REC.flag         = (int8_t)0xFF;
    NETWORK_$FAILURE_REC.node_id      = 0xAABBCCDDu;
    NETWORK_$FAILURE_REC.failure_type = 3;
    NETWORK_$ACTIVITY_FLAG = (int8_t)0xFF;  /* active: it is cleared */
    run_local(0x2F, NULL);
    ASSERT_EQ(0, NETWORK_$FAILURE_REC.flag);
    ASSERT_EQ(0x00, reply_bytes[0x0A]);
    {
        uint32_t node, type;
        memcpy(&node, reply_bytes + 0x0C, 4);
        memcpy(&type, reply_bytes + 0x14, 4);
        ASSERT_EQ(0xAABBCCDDu, node);
        ASSERT_EQ(3u, type);
    }
}

/*
 * 0x00E65328-0x00E65352: dbf runs the copy loop one more time than the count
 * in D0, so ROUTE_$NETBUF_ALLOC + 1 buckets reach reply+0x0E.  Bucket
 * ROUTE_$NETBUF_ALLOC + 1 must stay untouched.
 */
TEST(request_0x43_copies_netbuf_alloc_plus_one_buckets)
{
    unsigned i;

    ROUTE_$RTWIRED_DATA.netbuf_alloc = 5;
    ROUTE_$RTWIRED_DATA.q_oflo = 0x11223344u;
    for (i = 0; i < 8; i++) {
        ROUTE_$RTWIRED_DATA.q_depth[i] = 0xD0000000u + i;
    }
    memset(reply_bytes, 0x5A, sizeof(reply));

    run_local(0x43, NULL);

    ASSERT_EQ(5, reply_word_at(0x08));
    {
        uint32_t v;
        memcpy(&v, reply_bytes + 0x0A, 4);
        ASSERT_EQ(0x11223344u, v);
        for (i = 0; i <= 5; i++) {
            memcpy(&v, reply_bytes + 0x0E + i * 4, 4);
            ASSERT_EQ(0xD0000000u + i, v);
        }
        memcpy(&v, reply_bytes + 0x0E + 6 * 4, 4);
        ASSERT_EQ(0x5A5A5A5Au, v);
    }
}

/*
 * 0x00E64BB0-0x00E64BC4: a pid outside 1..0x40 is refused with 0x000A0001 in
 * the REPLY's status word, and PROC2_$INFO is never reached.  A pid inside
 * the range reaches it with the constant cells as its first and fourth
 * arguments.
 */
TEST(request_0x21_range_checks_the_pid)
{
    uid_t param;
    status_$t status;

    memset(&param, 0, sizeof(param));
    /* the pid is the WORD at param+0x00 ("move.w (A3),D3w" at 0x00E64BB0),
     * so it is written by byte offset rather than through uid_t.high. */
    ((uint16_t *)&param)[0] = 0x41;
    status = run_local(0x21, &param);
    ASSERT_EQ(0u, status);
    ASSERT_EQ((uint32_t)status_$illegal_process_id, reply[1]);
    ASSERT_EQ(0, mock_proc_info_calls);

    reset_mocks();
    memset(&param, 0, sizeof(param));
    ((uint16_t *)&param)[0] = 5;
    status = run_local(0x21, &param);
    ASSERT_EQ(0u, status);
    ASSERT_EQ(0u, reply[1]);
    ASSERT_EQ(1, mock_proc_info_calls);
    ASSERT_EQ(0, mock_proc_info_scan_key);      /* 0x00E658C4 */
    ASSERT_EQ(5, mock_proc_info_pid);
    ASSERT_EQ(0x01F8, mock_proc_info_len);      /* 0x00E658C6 */
}

/*
 * 0x00E64F8E-0x00E64FC2: the jump table at 0x00E64FA6 has four entries;
 * selectors 0 and 1 signal one process, 2 and 3 a process group, and
 * anything >= 4 falls out of range at 0x00E64F98 into the default arm, which
 * writes the CALLER'S status.
 */
TEST(request_0x35_dispatches_on_the_selector_longword)
{
    uid_t param[2];
    status_$t status;

    memset(param, 0, sizeof(param));
    param[1].low = 1;                           /* the longword at param+0x0C */
    status = run_local(0x35, param);
    ASSERT_EQ(0u, status);
    ASSERT_EQ(1, mock_signal_os_calls);
    ASSERT_EQ(0, mock_signal_pgroup_calls);
    ASSERT_EQ(0x13, mock_signal_number);        /* 0x00E658C8 */

    reset_mocks();
    memset(param, 0, sizeof(param));
    param[1].low = 3;
    status = run_local(0x35, param);
    ASSERT_EQ(0u, status);
    ASSERT_EQ(0, mock_signal_os_calls);
    ASSERT_EQ(1, mock_signal_pgroup_calls);
    ASSERT_EQ(0x13, mock_signal_number);

    reset_mocks();
    memset(param, 0, sizeof(param));
    param[1].low = 4;
    status = run_local(0x35, param);
    ASSERT_EQ(status_$network_unknown_request_type, status);
    ASSERT_EQ(0, mock_signal_os_calls);
    ASSERT_EQ(0, mock_signal_pgroup_calls);
    ASSERT_EQ(0u, reply[1]);
}

/*
 * 0x00E64916-0x00E64956: four DISK_$GET_STATS calls with controller type 4,
 * controller 0 and the unit as the third word, each 22 bytes packed 22 bytes
 * apart from reply+0x08.
 */
TEST(request_0x10_packs_four_disk_records)
{
    unsigned unit;

    memset(reply_bytes, 0x5A, sizeof(reply));
    run_local(0x10, NULL);

    ASSERT_EQ(4, mock_disk_get_stats_calls);
    ASSERT_EQ(4, mock_disk_ctype);
    ASSERT_EQ(0, mock_disk_cnum);
    ASSERT_EQ(3, mock_disk_unit);               /* the last call */
    for (unit = 0; unit < 4; unit++) {
        unsigned base = 0x08 + unit * 0x16;
        ASSERT_EQ((uint8_t)(0x10 + unit), reply_bytes[base]);
        ASSERT_EQ((uint8_t)(0x10 + unit), reply_bytes[base + 0x15]);
    }
    /* nothing past the fourth record */
    ASSERT_EQ(0x5A, reply_bytes[0x08 + 4 * 0x16]);
}

/*
 * 0x00E64C00-0x00E64C4A: the ring-log command is chosen by the parameter
 * block's first word, and the network log is only started when the ring log
 * reported success.  Both write the CALLER'S status ("pea (A4)").
 */
TEST(request_0x25_drives_both_logs)
{
    uid_t param[2];
    status_$t status;

    memset(param, 0, sizeof(param));
    /* first word 0 -> RINGLOG_CMD_CLEAR (3); NETLOG cmd at param+0x02 */
    ((uint16_t *)param)[1] = 2;             /* param+0x02 */
    ((uint32_t *)param)[2] = 0;             /* param+0x08 */
    memcpy((uint8_t *)param + 0x0A, "\xDE\xAD\xBE\xEF", 4);
    status = run_local(0x25, param);
    ASSERT_EQ(0u, status);
    ASSERT_EQ(1, mock_ringlog_calls);
    ASSERT_EQ(3, mock_ringlog_cmd);
    ASSERT_EQ(1, mock_netlog_calls);
    ASSERT_EQ(2, mock_netlog_cmd);
    {
        uint32_t expect;
        memcpy(&expect, (uint8_t *)param + 0x0A, 4);
        ASSERT_EQ(expect, mock_netlog_kinds);
    }

    reset_mocks();
    memset(param, 0, sizeof(param));
    ((uint16_t *)param)[0] = 1;             /* non-zero -> RINGLOG_CMD_STOP */
    run_local(0x25, param);
    ASSERT_EQ(4, mock_ringlog_cmd);

    /* a failed ring-log call stops the arm before NETLOG (0x00E64C26) */
    reset_mocks();
    memset(param, 0, sizeof(param));
    mock_ringlog_status = 0x00110001;
    status = run_local(0x25, param);
    ASSERT_EQ(0x00110001u, status);
    ASSERT_EQ(1, mock_ringlog_calls);
    ASSERT_EQ(0, mock_netlog_calls);
}

/*
 * The cmpi chain at 0x00E6463E-0x00E64786 has no 0x45 entry, so the
 * time-sync request the SERVER answers is unknown here and takes the default
 * arm.  The tree used to give it a silent no-op arm.
 */
TEST(request_0x45_is_not_in_the_dispatch_chain)
{
    status_$t status = run_local(0x45, NULL);
    ASSERT_EQ(status_$network_unknown_request_type, status);
    ASSERT_EQ(0u, reply[1]);
    ASSERT_EQ(0x46, reply_word_at(0x02));
}

int main(void)
{
    printf("ASKNODE_$INTERNET_INFO tests\n");
    RUN_TEST(mem_rec_is_the_0x56_bytes_the_loop_moves);
    RUN_TEST(stats_copies_whole_mem_rec_to_reply_0x72);
    RUN_TEST(stats_mem_rec_copy_does_not_overrun);
    RUN_TEST(stats_reply_carries_the_page_error_table);
    RUN_TEST(stats_real_pages_word_follows_the_copy);
    RUN_TEST(node_uid_reply_carries_the_uid_complements);
    RUN_TEST(unknown_request_writes_status_ret_not_the_reply_status);
    RUN_TEST(constant_cells_match_the_code_segment_pool);
    RUN_TEST(request_0x37_returns_the_route_port);
    RUN_TEST(request_0x2f_copies_the_failure_record_and_clears_the_flag);
    RUN_TEST(request_0x43_copies_netbuf_alloc_plus_one_buckets);
    RUN_TEST(request_0x21_range_checks_the_pid);
    RUN_TEST(request_0x35_dispatches_on_the_selector_longword);
    RUN_TEST(request_0x10_packs_four_disk_records);
    RUN_TEST(request_0x25_drives_both_logs);
    RUN_TEST(request_0x45_is_not_in_the_dispatch_chain);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
