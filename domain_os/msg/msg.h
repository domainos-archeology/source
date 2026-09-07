/*
 * MSG_$ - Message Passing / IPC Subsystem
 *
 * Provides inter-process communication through message sockets.
 * Supports both local and network message passing.
 */

#ifndef MSG_MSG_H
#define MSG_MSG_H

#include "os/os.h"
#include "net_io/net_io.h"   /* net_io_$send_info_t, used by MSG_$SEND_HW */

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
void MSG_$OPEN(msg_$socket_t *socket, int16_t *depth, status_$t *status_ret);

/* Open a message socket (internal) */
void MSG_$OPENI(msg_$socket_t *socket, int16_t *depth, status_$t *status_ret);

/* Close a message socket */
void MSG_$CLOSE(msg_$socket_t *socket, status_$t *status_ret);

/* Close a message socket (internal) */
void MSG_$CLOSEI(msg_$socket_t *socket, status_$t *status_ret);

/* Allocate a specific socket number (returns true on success) */
int8_t MSG_$ALLOCATE(msg_$socket_t *socket, int16_t *depth, status_$t *status_ret);

/* Allocate a specific socket number (internal) */
void MSG_$ALLOCATEI(msg_$socket_t *socket, int16_t *depth, status_$t *status_ret);

/*
 * Wait for message on socket (returns true == 0xFF when the wait succeeded)
 *
 * Takes exactly two arguments: 0x00E59BA4 does "link.w A6,-0x8" and
 * "pea (-0x4,A6)" to allocate the status_$t locally, pushes only (0x8,A6) and
 * (0xc,A6), and returns "seq D0b" on that local status.  It never writes a
 * caller-supplied status.  Reached through SVC_$TRAP2_TABLE[0x15], which is a
 * two-argument dispatcher.
 *
 * timeout points at a 16-bit tick count added to TIME_$CLOCKH to form the
 * timeout deadline (only a word is read: 0x00E59C4E "move.w (A4),D1w").
 */
boolean MSG_$WAIT(msg_$socket_t *socket, int16_t *timeout);

/* Wait for message on socket (internal) */
void MSG_$WAITI(msg_$socket_t *socket, int16_t *timeout, status_$t *status_ret);

/*
 * MSG_$RCV / MSG_$RCVI - receive a message.
 *
 * Argument shapes recovered from the two prologues and from
 * MSG_$$RCV_INTERNAL's (0x00E59548, see msg/msg_internal.h).  MSG_$RCVI has
 * 16 arguments at 0x08..0x44 and MSG_$RCV 12 at 0x08..0x34; MSG_$RCV
 * supplies locals for the four destination fields and for the whole
 * msg_$hw_addr_t record, returning only its first word (0x00E59540).
 *
 * Both "max length" arguments are passed BY REFERENCE here and dereferenced
 * by MSG_$RCVI (0x00E59712 / 0x00E59720 `move.w (An),-(SP)`).
 *
 * The record's declaration is below, so these prototypes appear after it.
 */

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

/* Receive a message */
void MSG_$RCV(msg_$socket_t *socket,
              uint32_t *src_node,
              uint16_t *src_sock,
              uint16_t *proto_family_ret,
              uint16_t *msg_type,
              void *template,
              uint16_t *template_max,
              uint16_t *template_len_ret,
              void *data,
              uint16_t *data_max,
              uint16_t *data_len_ret,
              status_$t *status_ret);

/* Receive a message (internal) */
void MSG_$RCVI(msg_$socket_t *socket,
               uint32_t *dest_net,
               uint32_t *dest_node,
               uint16_t *dest_sock,
               uint32_t *src_net,
               uint32_t *src_node,
               uint16_t *src_sock,
               msg_$hw_addr_t *hw_addr,
               uint16_t *msg_type,
               void *template,
               uint16_t *template_max,
               uint16_t *template_len_ret,
               void *data,
               uint16_t *data_max,
               uint16_t *data_len_ret,
               status_$t *status_ret);

/*
 * Receive a message into one contiguous buffer (0x00E597A6).
 *
 * Thirteen arguments at 0x08..0x38; the same address vocabulary as
 * MSG_$RCVI, with the template and the payload concatenated into data_buf.
 */
void MSG_$RCV_CONTIGI(msg_$socket_t *socket,
                      uint32_t *dest_net,
                      uint32_t *dest_node,
                      uint16_t *dest_sock,
                      uint32_t *src_net,
                      uint32_t *src_node,
                      uint16_t *src_sock,
                      msg_$hw_addr_t *hw_addr,
                      uint16_t *msg_type,
                      char *data_buf,
                      uint16_t *max_len,
                      uint16_t *data_len,
                      status_$t *status);

/*
 * The short form (0x00E59756), nine arguments at 0x08..0x28.  Note that
 * data_buf_ptr is a pointer to the buffer POINTER (0x00E59760
 * "movea.l (A0),A2").
 */
void MSG_$RCV_CONTIG(msg_$socket_t *socket,
                     uint32_t *src_node,
                     uint16_t *src_sock,
                     uint16_t *proto_family_ret,
                     uint16_t *msg_type,
                     char **data_buf_ptr,
                     uint16_t *max_len,
                     uint16_t *data_len,
                     status_$t *status);

/*
 * Receive message, also reporting the two netbuf event-count words.
 *
 * Fourteen arguments at 0x08..0x3C (0x00E59950); the shape is MSG_$RCV's
 * plus ec_param1_ret / ec_param2_ret before the status.
 */
void MSG_$RCV_HW(msg_$socket_t *socket,
                 uint32_t *src_node,
                 uint16_t *src_sock,
                 uint16_t *proto_family_ret,
                 uint16_t *msg_type,
                 void *template,
                 uint16_t *template_max,
                 uint16_t *template_len_ret,
                 void *data,
                 uint16_t *data_max,
                 uint16_t *data_len_ret,
                 uint16_t *ec_param1_ret,
                 uint16_t *ec_param2_ret,
                 status_$t *status);

/*
 * MSG_$SEND - send a message to a socket on another node (0x00E599FC)
 *
 * Eleven arguments, every one of them a pointer; the routine copies the
 * 30-byte packet-info template out of MSG_$DATA, drops the caller's flags
 * word into it (0x00E59A50) and hands the whole thing to MSG_$$SEND with
 * port -1, routing key 0 and both source addresses set to NODE_$ME
 * (0x00E59A54-0x00E59A8E).
 *
 * @param dest_node     Destination node id      ((0x8,A6), 0x00E59A0E)
 * @param dest_sock     Destination socket, word ((0xc,A6), 0x00E59A16)
 * @param src_sock      Source socket, word      ((0x10,A6), 0x00E59A1E)
 * @param info_flags    Packet-info flags word written over the template's
 *                      first word ((0x14,A6), 0x00E59A2A / 0x00E59A50)
 * @param request_id    Request id, word         ((0x18,A6), 0x00E59A2C)
 * @param template      Request template         ((0x1c,A6), passed by value)
 * @param template_len  Template length, word    ((0x20,A6), 0x00E59A34)
 * @param data          Payload                  ((0x24,A6), passed by value)
 * @param data_len      Payload length, word     ((0x28,A6), 0x00E59A3C)
 * @param xmit_status   Output: net_io_$send_info_t.xmit_status, the SECOND
 *                      word of the send-info record ((0x2c,A6), 0x00E59A98
 *                      "move.w (-0x22,A6),(A1)")
 * @param status_ret    Output: status code      ((0x30,A6))
 */
void MSG_$SEND(uint32_t *dest_node,
               uint16_t *dest_sock,
               uint16_t *src_sock,
               uint16_t *info_flags,
               uint16_t *request_id,
               void *template,
               uint16_t *template_len,
               void *data,
               uint16_t *data_len,
               uint16_t *xmit_status,
               status_$t *status_ret);

/*
 * MSG_$SENDI - the by-reference form of MSG_$$SEND (0x00E59AA6)
 *
 * Fourteen arguments.  Every scalar is dereferenced on the way through; the
 * packet-info record, the template and the payload are passed on by value
 * (0x00E59AAE-0x00E59AF8).  The port number is fixed at -1.
 *
 * @param xmit_status   Output: net_io_$send_info_t.xmit_status, again the
 *                      second word of the record MSG_$$SEND filled in
 *                      ((0x38,A6), 0x00E59B06 "move.w (-0x2,A6),(A0)")
 */
void MSG_$SENDI(uint32_t *routing_key,
                uint32_t *dest_node,
                uint16_t *dest_sock,
                int32_t *src_node_or,
                uint32_t *src_node,
                uint16_t *src_sock,
                void *pkt_info,
                uint16_t *request_id,
                void *template,
                uint16_t *template_len,
                void *data,
                uint16_t *data_len,
                uint16_t *xmit_status,
                status_$t *status_ret);

/*
 * Send a message to an explicit network/socket pair (0x00E59B14).
 *
 * Fifteen arguments; the same by-reference shape as MSG_$SENDI with a
 * msg_$hw_addr_t in front, whose proto_subtype (+0x06) and reserved1 (+0x08)
 * are the network and socket handed to ROUTE_$FIND_PORT.
 */
void MSG_$SEND_HW(msg_$hw_addr_t *hw_addr,
                  uint32_t *routing_key,
                  uint32_t *dest_node,
                  uint16_t *dest_sock,
                  int32_t *src_node_or,
                  uint32_t *src_node,
                  uint16_t *src_sock,
                  void *pkt_info,
                  uint16_t *request_id,
                  void *template,
                  uint16_t *template_len,
                  void *data,
                  uint16_t *data_len,
                  net_io_$send_info_t *send_info,
                  status_$t *status);

/*
 * Send and receive (combined operation), 0x00E59D52.
 *
 * Seventeen arguments at 0x08..0x48.  MSG_$SAR owns neither socket nor
 * packet-info record: it allocates a temporary user socket inside
 * MSG_$SARI and builds the 30-byte packet info from msg_$data_t's template
 * with the caller's flags word on top (0x00E59D5E-0x00E59D72).
 *
 * @param timeout   a tick count added to TIME_$CLOCKH, SIGN extended
 * @param flags     the word that overwrites the packet-info template's first
 * @param proto_family_ret  msg_$hw_addr_t.proto_family from the reply
 */
void MSG_$SAR(int16_t *timeout,
              uint32_t *dest_node,
              uint16_t *dest_sock,
              uint16_t *flags,
              void *send_template,
              uint16_t *send_template_len,
              void *send_data,
              uint16_t *send_data_len,
              uint16_t *xmit_status_ret,
              uint16_t *proto_family_ret,
              void *rcv_template,
              uint16_t *rcv_template_max,
              uint16_t *rcv_template_len_ret,
              void *rcv_data,
              uint16_t *rcv_data_max,
              uint16_t *rcv_data_len_ret,
              status_$t *status_ret);

/*
 * Send and receive (internal), 0x00E59DD4.  Eighteen arguments at 0x08..0x4C;
 * the packet-info record and the msg_$hw_addr_t are the caller's here.
 */
void MSG_$SARI(int16_t *timeout,
               uint32_t *routing_key,
               uint32_t *dest_node,
               uint16_t *dest_sock,
               void *pkt_info,
               void *send_template,
               uint16_t *send_template_len,
               void *send_data,
               uint16_t *send_data_len,
               uint16_t *xmit_status_ret,
               msg_$hw_addr_t *hw_addr,
               void *rcv_template,
               uint16_t *rcv_template_max,
               uint16_t *rcv_template_len_ret,
               void *rcv_data,
               uint16_t *rcv_data_max,
               uint16_t *rcv_data_len_ret,
               status_$t *status_ret);

/* Get event count for socket */
void MSG_$GET_EC(msg_$socket_t *socket, uint32_t *ec, status_$t *status);

/* Test if message is available on socket (returns non-zero if message pending) */
boolean MSG_$TEST_FOR_MESSAGE(msg_$socket_t *socket, uint32_t *ec_value,
                               status_$t *status_ret);

/* Share socket with another address space */
void MSG_$SHARE_SOCKET(msg_$socket_t *socket, uid_t *uid, int16_t *add_remove,
                        status_$t *status_ret);

/* Duplicate socket ownership for fork (returns non-zero if any sockets shared) */
boolean MSG_$FORK(uint16_t *parent_asid, uint16_t *child_asid);

/* Free ASID resources */
void MSG_$FREE_ASID(uint16_t *asid_ptr);

/* Get local network ID */
void MSG_$GET_MY_NET(uint32_t *net_id);

/* Get local node ID */
void MSG_$GET_MY_NODE(uint32_t *node_id);

/* Set HPIPC socket ownership */
void MSG_$SET_HPIPC(msg_$socket_t *socket, void *param2, status_$t *status_ret);

#endif /* MSG_MSG_H */
