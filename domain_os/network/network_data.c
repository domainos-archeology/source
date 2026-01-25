/*
 * NETWORK Data - Global variables for NETWORK subsystem
 *
 * Original m68k addresses documented in comments.
 * Network data area base: 0xE248FC
 */

#include "network/network_internal.h"

/*
 * Network table - maps network indices to network IDs
 *
 * 64 entries, each 8 bytes (refcount:4, net_id:4)
 * Base addresses:
 *   refcount: 0xE24934 (A5+0x38, where A5=0xE248FC)
 *   net_id:   0xE24938 (A5+0x3C)
 */
network_table_entry_t NETWORK_$NET_TABLE[NETWORK_TABLE_SIZE];

/*
 * Server counts
 */
int16_t NETWORK_$REQUEST_SERVER_CNT;  /* 0xE24C1C (+0x320) */
int16_t NETWORK_$PAGE_SERVER_CNT;     /* 0xE24C1E (+0x322) */

/*
 * Service configuration
 */
uint32_t NETWORK_$ALLOWED_SERVICE;    /* 0xE24C3E (+0x342) */
int16_t NETWORK_$REMOTE_POOL;         /* 0xE24C40 (+0x344) */

/*
 * Mode flags
 */
int8_t NETWORK_$ACTIVITY_FLAG;        /* 0xE24C46 (+0x34A) */
int8_t NETWORK_$USER_SOCK_OPEN;       /* 0xE24C48 (+0x34C) */
int8_t NETWORK_$REALLY_DISKLESS;      /* 0xE24C4A (+0x34E) */
int8_t NETWORK_$DISKLESS;             /* 0xE24C4C (+0x350) */

/*
 * Mother node ID
 */
uint32_t NETWORK_$MOTHER_NODE;        /* 0xE24C0C */

/*
 * Paging file UID
 */
uid_t NETWORK_$PAGING_FILE_UID;

/*
 * Statistics counters
 */
uint16_t NETWORK_$INFO_RQST_CNT;
uint16_t NETWORK_$PAGIN_RQST_CNT;
uint16_t NETWORK_$MULT_PAGIN_RQST_CNT;
uint16_t NETWORK_$PAGOUT_RQST_CNT;
uint16_t NETWORK_$READ_CALL_CNT;
uint16_t NETWORK_$WRITE_CALL_CNT;
uint16_t NETWORK_$READ_VIOL_CNT;
uint16_t NETWORK_$WRITE_VIOL_CNT;
uint16_t NETWORK_$BAD_CHKSUM_CNT;

/*
 * Retry timeout
 */
int16_t NETWORK_$RETRY_TIMEOUT;       /* 0xE24C18 */

/*
 * Loopback flag (non-M68K only - M68K uses direct memory access)
 */
#if !defined(M68K)
int8_t NETWORK_$LOOPBACK_FLAG;
uint32_t NODE_$ME;
#endif
