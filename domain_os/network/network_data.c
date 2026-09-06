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
 * In the binary this is not a variable of its own: 0xE24C3F is byte 1 of the
 * NETWORK_$ALLOWED_SERVICE longword at 0xE24C3E, so
 *   NETWORK_$CAPABLE_FLAGS bit n  ==  NETWORK_$ALLOWED_SERVICE bit (16 + n)
 * on the big-endian m68k.  NETWORK_$SET_SERVICE writes the whole longword and
 * REM_FILE_$SEND_REQUEST / ASKNODE_$INTERNET_INFO read the byte, so the two
 * names must denote the same storage.
 *
 * A macro would be the portable spelling, but rem_file/rem_file_internal.h
 * re-declares NETWORK_$CAPABLE_FLAGS as an object, so the sharing is done
 * with an assembler symbol equate instead.  That needs an ELF toolchain and a
 * big-endian target; anywhere else the byte becomes a separate variable and
 * the aliasing is a documented constraint (a write through one name is not
 * visible through the other).  TODO(source-yh2): drop the duplicate
 * declaration in rem_file_internal.h and the two names can share a macro.
 */
#if defined(__GNUC__) && defined(__ELF__) && \
    defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
__asm__(".globl NETWORK_$CAPABLE_FLAGS\n\t"
        ".set NETWORK_$CAPABLE_FLAGS, NETWORK_$ALLOWED_SERVICE + 1");
#else
uint8_t NETWORK_$CAPABLE_FLAGS;
#endif

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

/*
 * Loopback flag (non-M68K only - M68K uses direct memory access)
 */
#if !defined(ARCH_M68K)
int8_t NETWORK_$LOOPBACK_FLAG;
/* NODE_$ME (0xE245A4) is defined in uid/uid_data.c */
#endif
