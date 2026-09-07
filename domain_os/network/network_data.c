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
/* NETWORK_$REMOTE_POOL is bits 0..15 of NETWORK_$ALLOWED_SERVICE; see network.h. */

/*
 * Mode flags
 */
int8_t NETWORK_$ACTIVITY_FLAG;        /* 0xE24C42 (+0x346) */
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
uint32_t NETWORK_$PAGING_BACKLOG;       /* 0xE24BAC */
uint32_t NETWORK_$FILE_BACKLOG;         /* 0xE24BD0 */
uint16_t NETWORK_$RCV_READ_AHEAD;       /* 0xE24C26 */
uint16_t NETWORK_$MULT_PAGIN_RQST_CNT;  /* 0xE24C28 */
uint16_t NETWORK_$BAD_CHKSUM_CNT;       /* 0xE24C2A */
uint16_t NETWORK_$READ_VIOL_CNT;        /* 0xE24C2C */
uint16_t NETWORK_$WRITE_VIOL_CNT;       /* 0xE24C2E */
uint16_t NETWORK_$READ_CALL_CNT;        /* 0xE24C30 */
uint16_t NETWORK_$WRITE_CALL_CNT;       /* 0xE24C32 */
uint16_t NETWORK_$SET_ATTRIB_CALL_CNT;  /* 0xE24C34 */
uint16_t NETWORK_$ATTRIB_RQST_CNT;      /* 0xE24C36 */
uint16_t NETWORK_$INFO_RQST_CNT;        /* 0xE24C38 */
uint16_t NETWORK_$PAGIN_RQST_CNT;       /* 0xE24C3A */
uint16_t NETWORK_$PAGOUT_RQST_CNT;      /* 0xE24C3C */

/*
 * Network capability flags (0xE24C3F)
 *
 * No storage is defined here.  0xE24C3F is byte 1 of the
 * NETWORK_$ALLOWED_SERVICE longword above, so NETWORK_$CAPABLE_FLAGS is a
 * macro in network/network.h that extracts bits 16..23 of this variable.
 */

/*
 * Network failure record (0xE24BF4, 16 bytes)
 */
network_$failure_rec_t NETWORK_$FAILURE_REC;

/*
 * Retry timeout
 */
int16_t NETWORK_$RETRY_TIMEOUT;       /* 0xE24C18 */

/*
 * Spin lock for network data protection
 */
void *NETWORK_$LOCK;                  /* 0xE24BA0 (+0x2A4) */

/* NODE_$ME (0xE245A4) is defined in uid/uid_data.c */

/*
 * ============================================================================
 * NETWORK_ module block flags and pointers
 *
 * The SAU2 map's `D E248FC NETWORK size = 364` segment names each of these,
 * and the distance to the next named symbol fixes each one's width:
 *
 *   E24BD0  NETWORK_$FILE_BACKLOG          (to NETWORK_$FAILURE_REC, 0xE24BF4)
 *   E24C42  NETWORK_$ACTIVITY_FLAG
 *   E24C44  NETWORK_$LOOPBACK_FLAG         (2 bytes)
 *   E24C46  NETWORK_$DO_CHKSUM             (2 bytes)
 *   E24C48  NETWORK_$USER_SOCK_OPEN
 *
 * All of them are zero in the image.
 * ============================================================================
 */

/*
 * NETWORK_$LOOPBACK_FLAG - Domain boolean; when set (< 0) the network layer
 * loops packets addressed to this node back internally.
 *
 * Original address: 0xE24C44
 */
int8_t NETWORK_$LOOPBACK_FLAG;

/*
 * NETWORK_$DO_CHKSUM - Domain boolean enabling packet checksums.
 *
 * Original address: 0xE24C46
 */
char NETWORK_$DO_CHKSUM;

/*
 * NETWORK_$FILE_BACKLOG_OVERFLOW - the overflow counter at the end of the
 * NETWORK_$FILE_BACKLOG record (0xE24BD0 + 0x20); the record runs up to
 * NETWORK_$FAILURE_REC at 0xE24BF4, so this longword is its last field.
 *
 * Original address: 0xE24BF0
 */
uint32_t NETWORK_$FILE_BACKLOG_OVERFLOW;

/*
 * NETWORK_$SERVICE_INFO_PTR - pointer to the network service-info record.
 *
 * It lives in the SOCK data segment (`D E27510 SOCK size = 1C28`), one
 * longword past SOCK_$SOCKET_PTR (0xE28DB4).  NULL in the image.
 *
 * Original address: 0xE28DB8
 */
uint8_t *NETWORK_$SERVICE_INFO_PTR;
