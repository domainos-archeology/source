/*
 * MSG_$ - Message Passing / IPC Subsystem
 *
 * Provides inter-process communication through message sockets.
 * Supports both local and network message passing.
 */

#ifndef MSG_MSG_H
#define MSG_MSG_H

#include "os/os.h"

/*
 * Constants
 */
#define MSG_MAX_SOCKET      224     /* Maximum socket number (0xE0) */
#define MSG_MAX_DEPTH       32      /* Maximum socket depth (0x21 - 1) */
#define MSG_MAX_ASID        64      /* Maximum address space IDs */

/*
 * Status codes (module 0x29)
 */
#define status_$msg_socket_out_of_range     0x00290001
#define status_$msg_too_deep                0x00290002
#define status_$msg_no_more_sockets         0x00290004
#define status_$msg_no_owner                0x00290005
#define status_$msg_socket_in_use           0x00290008
#define status_$msg_time_out                0x00290009
#define status_$msg_quit_fault              0x0029000a

/*
 * Message options/flags
 */
#define MSG_$OPTION_WAIT    0x0001  /* Wait for message */
#define MSG_$OPTION_NOWAIT  0x0000  /* Don't wait */

/*
 * Socket types
 */
typedef uint16_t msg_$socket_t;

/*
 * Message descriptor for send/receive operations
 */
typedef struct msg_$desc_s {
    void *data;             /* Pointer to message data */
    uint32_t length;        /* Message length */
    uint32_t sender_net;    /* Sender network ID */
    uint32_t sender_node;   /* Sender node ID */
    uint16_t sender_socket; /* Sender socket */
    uint16_t flags;         /* Message flags */
} msg_$desc_t;

/*
 * Time specification for wait operations
 */
typedef struct msg_$time_s {
    uint32_t seconds;       /* Seconds */
    uint32_t microseconds;  /* Microseconds */
} msg_$time_t;

/*
 * Public API functions
 */

/* Initialize MSG subsystem */
void MSG_$INIT(void);

/* Open a message socket */
void MSG_$OPEN(msg_$socket_t *socket, status_$t *status);

/* Open a message socket (internal, returns status) */
status_$t MSG_$OPENI(msg_$socket_t *socket);

/* Close a message socket */
void MSG_$CLOSE(msg_$socket_t *socket, status_$t *status);

/* Close a message socket (internal, returns status) */
status_$t MSG_$CLOSEI(msg_$socket_t *socket);

/* Allocate a specific socket number */
void MSG_$ALLOCATE(msg_$socket_t *socket, status_$t *status);

/* Allocate a specific socket number (internal, returns status) */
status_$t MSG_$ALLOCATEI(msg_$socket_t *socket);

/* Wait for message on socket */
void MSG_$WAIT(msg_$socket_t *socket, msg_$time_t *timeout, status_$t *status);

/* Wait for message on socket (internal, returns status) */
status_$t MSG_$WAITI(msg_$socket_t *socket, msg_$time_t *timeout);

/* Receive a message */
void MSG_$RCV(msg_$socket_t *socket, void *buffer, uint32_t *length,
              msg_$desc_t *desc, status_$t *status);

/* Receive a message (internal, returns status) */
status_$t MSG_$RCVI(msg_$socket_t *socket, void *buffer, uint32_t *length,
                    msg_$desc_t *desc);

/*
 * Hardware address info structure for MSG_$RCV_CONTIGI and MSG_$RCV_HW
 *
 * Contains extended protocol and address information from received messages.
 */
typedef struct msg_$hw_addr_s {
    uint16_t proto_family;      /* Protocol family */
    uint16_t flags;             /* Flags from header */
    uint16_t proto_type;        /* Protocol type */
    uint16_t proto_subtype;     /* Protocol subtype */
    uint16_t reserved1;         /* Reserved */
    uint16_t reserved2;         /* Reserved (0) */
    uint16_t reserved3;         /* Reserved (0xFFFF) */
    uint8_t  inet_addr[16];     /* Internet address (if applicable) */
} msg_$hw_addr_t;

/* Receive contiguous message with hardware address info (internal) */
void MSG_$RCV_CONTIGI(msg_$socket_t *socket,
                      uint32_t *dest_proc,
                      uint32_t *dest_node,
                      uint16_t *dest_sock,
                      uint32_t *src_proc,
                      uint32_t *src_node,
                      uint16_t *src_sock,
                      msg_$hw_addr_t *hw_addr,
                      uint16_t *msg_type,
                      char *data_buf,
                      uint16_t *max_len,
                      uint16_t *data_len,
                      status_$t *status);

/* Receive contiguous message wrapper */
void MSG_$RCV_CONTIG(msg_$socket_t *socket,
                     uint32_t *dest_node,
                     uint32_t *dest_sock_out,
                     uint16_t *hw_addr_ret,
                     uint32_t *src_node,
                     uint32_t **src_node_ptr,
                     uint16_t *msg_type,
                     uint16_t *data_len,
                     status_$t *status);

/* Receive message with hardware address info */
void MSG_$RCV_HW(msg_$socket_t *socket,
                 uint32_t *dest_node,
                 uint32_t *dest_sock,
                 uint16_t *hw_addr_ret,
                 uint32_t *src_node,
                 uint32_t *src_sock,
                 uint16_t *msg_type_ptr,
                 uint16_t *hw_addr_buf,
                 void *data_buf_ptr,
                 uint32_t *src_node2,
                 uint16_t *max_data_len,
                 void *overflow_buf,
                 void *overflow_info,
                 uint16_t *max_overflow,
                 status_$t *status);

/* Send a message */
void MSG_$SEND(msg_$socket_t *socket, void *buffer, uint32_t length,
               msg_$desc_t *desc, status_$t *status);

/* Send a message (internal, returns status) */
status_$t MSG_$SENDI(msg_$socket_t *socket, void *buffer, uint32_t length,
                     msg_$desc_t *desc);

/* Send message using hardware address routing */
void MSG_$SEND_HW(void *hw_addr_info,
                  uint32_t *dest_proc,
                  uint32_t *dest_node,
                  uint16_t *dest_sock,
                  uint32_t *src_proc,
                  uint32_t *src_node,
                  uint16_t *src_sock,
                  void *msg_desc,
                  uint16_t *type_val,
                  void *type_data,
                  uint16_t *header_len,
                  void *data_ptr,
                  uint16_t *data_len,
                  void *bytes_sent,
                  status_$t *status);

/* Send and receive (combined operation) */
void MSG_$SAR(msg_$socket_t *socket, void *send_buf, uint32_t send_len,
              void *recv_buf, uint32_t *recv_len, msg_$desc_t *desc,
              msg_$time_t *timeout, status_$t *status);

/* Send and receive (internal, returns status) */
status_$t MSG_$SARI(msg_$socket_t *socket, void *send_buf, uint32_t send_len,
                    void *recv_buf, uint32_t *recv_len, msg_$desc_t *desc,
                    msg_$time_t *timeout);

/* Get event count for socket */
void MSG_$GET_EC(msg_$socket_t *socket, uint32_t *ec, status_$t *status);

/* Test if message is available on socket */
void MSG_$TEST_FOR_MESSAGE(msg_$socket_t *socket, int16_t *available,
                           status_$t *status);

/* Share socket with another address space */
void MSG_$SHARE_SOCKET(msg_$socket_t *socket, uint8_t asid, status_$t *status);

/* Duplicate socket ownership for fork */
void MSG_$FORK(uint8_t parent_asid, uint8_t child_asid);

/* Free ASID resources */
void MSG_$FREE_ASID(uint8_t asid);

/* Get local network ID */
void MSG_$GET_MY_NET(uint32_t *net_id);

/* Get local node ID */
void MSG_$GET_MY_NODE(uint32_t *node_id);

/* Set HPIPC socket ownership */
void MSG_$SET_HPIPC(msg_$socket_t *socket, status_$t *status);

#endif /* MSG_MSG_H */
