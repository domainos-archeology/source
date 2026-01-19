/*
 * SIO - Serial I/O Module
 *
 * This module provides the low-level serial I/O interface for Domain/OS.
 * It handles character-based serial communications including:
 *   - Receive/transmit interrupt handling
 *   - Hardware flow control (CTS/RTS)
 *   - Carrier detect (DCD) monitoring
 *   - Parameter configuration (baud rate, parity, etc.)
 *
 * The SIO module sits below the TTY subsystem and provides the hardware
 * abstraction layer for serial devices (console, modems, terminals).
 *
 * SIO descriptor structure is 0x78 (120) bytes and contains:
 *   - Function pointers for device-specific operations
 *   - Transmit buffer management
 *   - Parameter settings
 *   - Event counts for synchronization
 *   - State flags for flow control
 */

#ifndef SIO_H
#define SIO_H

#include "base/base.h"
#include "ec/ec.h"

/*
 * ============================================================================
 * SIO Status Codes (module 0x36)
 * ============================================================================
 */
#define status_$sio_invalid_param           0x00360002  /* Invalid parameter value */
#define status_$sio_parity_error            0x00360004  /* Parity error detected */
#define status_$sio_framing_error           0x00360005  /* Framing error detected */
#define status_$sio_overrun_error           0x00360006  /* Receive overrun error */
#define status_$sio_break_detected          0x00360007  /* Break condition detected */
#define status_$sio_hardware_error          0x00360009  /* Hardware error */
#define status_$sio_quit_signalled          0x0036000a  /* Quit signal received */
#define status_$sio_dtr_drop                0x0036000b  /* DTR dropped (unused in code) */

/*
 * ============================================================================
 * SIO Constants
 * ============================================================================
 */

/* Transmit buffer special characters */
#define SIO_TSTART_DELAY_MARKER     0xFE    /* Marker for delay sequence */
#define SIO_TSTART_DELAY_CMD        0x00    /* Delay command byte */

/* Flow control flag bits at offset +0x53 */
#define SIO_CTRL_SOFT_FLOW          0x01    /* Software flow control (XON/XOFF) */
#define SIO_CTRL_CTS_FLOW           0x02    /* CTS hardware flow control */
#define SIO_CTRL_DCD_HANGUP         0x04    /* Hangup on DCD loss */
#define SIO_CTRL_RECV_ERROR         0x08    /* Receive error notification */

/* Interrupt enable mask bits at offset +0x57 */
#define SIO_INT_DCD_CHANGE          0x08    /* DCD change notification */
#define SIO_INT_CTS_CHANGE          0x10    /* CTS change notification */

/* Status flag bits at offset +0x67 */
#define SIO_STAT_DCD_NOTIFY         0x08    /* DCD change pending notification */
#define SIO_STAT_CTS_NOTIFY         0x10    /* CTS change pending notification */
#define SIO_STAT_RECV_ERROR         0x20    /* Receive error pending */

/* Transmit state flag bits at offset +0x75 */
#define SIO_XMIT_ACTIVE             0x01    /* Transmit in progress */
#define SIO_XMIT_CTS_BLOCKED        0x02    /* Blocked by CTS */
#define SIO_XMIT_INHIBITED          0x04    /* Transmit inhibited */
#define SIO_XMIT_DEFER_INHIBIT      0x20    /* Deferred transmit inhibit */
#define SIO_XMIT_DEFER_PENDING      0x40    /* Deferred operation pending */
#define SIO_XMIT_DEFER_COMPLETE     0x80    /* Deferred operation complete */

/* State flags at offset +0x74 */
#define SIO_STATE_DELAY_ACTIVE      0x10    /* Delay timer active */
#define SIO_STATE_BREAK_ACTIVE      0x20    /* Break active */
#define SIO_STATE_BREAK_PENDING     0x40    /* Break pending */

/* Parameter change mask bits */
#define SIO_PARAM_BAUD              0x0003  /* Baud rate (bits 0-1) */
#define SIO_PARAM_CHAR_SIZE         0x0004  /* Character size */
#define SIO_PARAM_STOP_BITS         0x0008  /* Stop bits */
#define SIO_PARAM_PARITY            0x0010  /* Parity */
#define SIO_PARAM_SOFT_FLOW         0x0020  /* Software flow control */
#define SIO_PARAM_CTS_FLOW          0x0040  /* CTS flow control */
#define SIO_PARAM_RTS_ASSERT        0x0200  /* RTS assertion */
#define SIO_PARAM_DTR_ASSERT        0x0400  /* DTR assertion */
#define SIO_PARAM_DCD_HANGUP        0x0800  /* DCD hangup */
#define SIO_PARAM_RECV_ERROR        0x1000  /* Receive error */
#define SIO_PARAM_BREAK_MASK        0x2000  /* Break character mask */
#define SIO_PARAM_DCD_NOTIFY        0x4000  /* DCD notification */

/*
 * ============================================================================
 * SIO Transmit Buffer Structure
 * ============================================================================
 *
 * Circular buffer for transmit data
 * Size: variable, header is 6 bytes
 */
typedef struct sio_txbuf {
    uint16_t    read_idx;       /* 0x00: Read index (consumer) */
    uint16_t    write_idx;      /* 0x02: Write index (producer) */
    uint16_t    size;           /* 0x04: Buffer size */
    uint8_t     data[1];        /* 0x05: Buffer data (variable size) */
} sio_txbuf_t;

/*
 * ============================================================================
 * SIO Parameter Block Structure
 * ============================================================================
 *
 * Serial port parameters - 0x16 bytes
 * Used with SIO_$K_SET_PARAM and SIO_$K_INQ_PARAM
 */
typedef struct sio_params {
    uint32_t    flags1;         /* 0x00: Control flags (flow control, etc.) */
    uint32_t    flags2;         /* 0x04: Extended flags */
    uint32_t    break_mask;     /* 0x08: Break character mask */
    uint32_t    baud_rate;      /* 0x0C: Baud rate setting */
    int16_t     char_size;      /* 0x10: Character size (0-3) */
    int16_t     stop_bits;      /* 0x12: Stop bits (1-3) */
    int16_t     parity;         /* 0x14: Parity setting (0-3) */
} sio_params_t;

/*
 * ============================================================================
 * SIO Descriptor Structure
 * ============================================================================
 *
 * Main SIO device descriptor - 0x78 (120) bytes
 * One per serial port
 */
typedef struct sio_desc {
    m68k_ptr_t  context;        /* 0x00: Device context/handle */
    m68k_ptr_t  owner;          /* 0x04: Owner handle (passed to callbacks) */
    m68k_ptr_t  reserved_08;    /* 0x08: Reserved */
    m68k_ptr_t  reserved_0c;    /* 0x0C: Reserved */
    m68k_ptr_t  reserved_10;    /* 0x10: Reserved */
    m68k_ptr_t  reserved_14;    /* 0x14: Reserved */
    m68k_ptr_t  reserved_18;    /* 0x18: Reserved */
    m68k_ptr_t  reserved_1c;    /* 0x1C: Reserved */
    m68k_ptr_t  reserved_20;    /* 0x20: Reserved */
    m68k_ptr_t  txbuf;          /* 0x24: Transmit buffer pointer */
    m68k_ptr_t  rcv_handler;    /* 0x28: Default receive handler */
    m68k_ptr_t  drain_handler;  /* 0x2C: Buffer drained handler */
    m68k_ptr_t  dcd_handler;    /* 0x30: DCD loss handler */
    m68k_ptr_t  special_rcv;    /* 0x34: Special receive handler */
    m68k_ptr_t  data_rcv;       /* 0x38: Data receive handler */
    m68k_ptr_t  output_char;    /* 0x3C: Output character function */
    m68k_ptr_t  set_params;     /* 0x40: Set parameters function */
    m68k_ptr_t  inq_params;     /* 0x44: Inquire parameters function */
    m68k_ptr_t  reserved_48;    /* 0x48: Reserved */

    /* Parameter block - 0x16 bytes */
    sio_params_t params;        /* 0x4C: Current parameters */
    uint16_t    reserved_62;    /* 0x62: Padding */

    uint32_t    pending_int;    /* 0x64: Pending interrupts */
    ec_$eventcount_t ec;        /* 0x68: Event count (12 bytes) */
    uint16_t    state;          /* 0x74: State flags */
    /* Note: offset 0x75 is the transmit state byte, accessed separately */
    uint16_t    reserved_76;    /* 0x76: Reserved */
} sio_desc_t;

/* Verify structure size (should be 0x78 = 120 bytes) */
_Static_assert(sizeof(sio_desc_t) == 0x78, "sio_desc_t must be 120 bytes");

/*
 * ============================================================================
 * SIO Public Function Declarations
 * ============================================================================
 */

/*
 * SIO_$INIT - Initialize a serial I/O port
 *
 * Initializes an SIO descriptor for the specified port.
 * Port 1 is the console, other ports are generic serial.
 *
 * Parameters:
 *   port_num - Port number (1 = console, others = generic)
 *   param2 - Unknown parameter
 *   param3 - Unknown parameter
 *   desc_ret - Pointer to receive SIO descriptor address
 *   flags - Initialization flags (bit 7 = enable crash handler)
 *   status_ret - Status return
 *
 * Original address: 0x00e32be0
 */
void SIO_$INIT(int16_t port_num, uint32_t param2, uint32_t param3,
               sio_desc_t **desc_ret, int8_t flags, status_$t *status_ret);

/*
 * ============================================================================
 * SIO Interrupt Handler Declarations (I_ prefix)
 * ============================================================================
 */

/*
 * SIO_$I_INIT - Initialize SIO descriptor fields
 *
 * Clears state, pending interrupts, and initializes the event count.
 *
 * Parameters:
 *   desc - SIO descriptor to initialize
 *
 * Original address: 0x00e67e5e
 */
void SIO_$I_INIT(sio_desc_t *desc);

/*
 * SIO_$I_RCV - Receive interrupt handler
 *
 * Called when data is received. Handles interrupt mask filtering
 * and dispatches to appropriate receive handlers.
 *
 * Parameters:
 *   desc - SIO descriptor
 *   char_data - Received character
 *   error_flags - Error flags from hardware
 *
 * Original address: 0x00e1c620
 */
void SIO_$I_RCV(sio_desc_t *desc, uint8_t char_data, uint32_t error_flags);

/*
 * SIO_$I_XMIT_DONE - Transmit complete interrupt handler
 *
 * Called when a character transmission completes.
 * Clears transmit-active flag and restarts transmitter.
 *
 * Parameters:
 *   desc - SIO descriptor
 *
 * Returns:
 *   Non-zero if transmit is active after call
 *
 * Original address: 0x00e1c6b4
 */
int8_t SIO_$I_XMIT_DONE(sio_desc_t *desc);

/*
 * SIO_$I_CTS_CHANGE - CTS signal change handler
 *
 * Called when Clear-To-Send signal changes state.
 * Handles flow control and notifies waiters.
 *
 * Parameters:
 *   desc - SIO descriptor
 *   cts_state - CTS state (negative = asserted)
 *
 * Original address: 0x00e1c6da
 */
void SIO_$I_CTS_CHANGE(sio_desc_t *desc, int8_t cts_state);

/*
 * SIO_$I_DCD_CHANGE - DCD signal change handler
 *
 * Called when Data Carrier Detect signal changes.
 * May trigger hangup and notifies waiters.
 *
 * Parameters:
 *   desc - SIO descriptor
 *   dcd_state - DCD state (negative = asserted)
 *
 * Original address: 0x00e1c73e
 */
void SIO_$I_DCD_CHANGE(sio_desc_t *desc, int8_t dcd_state);

/*
 * SIO_$I_TSTART - Start/continue transmission
 *
 * Main transmit state machine. Pulls characters from transmit
 * buffer and sends them, handling delays and flow control.
 *
 * Parameters:
 *   desc - SIO descriptor
 *
 * Returns:
 *   Result varies based on transmit state
 *
 * Original address: 0x00e1c7a8
 */
uint16_t SIO_$I_TSTART(sio_desc_t *desc);

/*
 * SIO_$I_INHIBIT_RCV - Control receive inhibit state
 *
 * Controls software flow control (XON/XOFF) for receive.
 * Updates hardware flow control if enabled.
 *
 * Parameters:
 *   desc - SIO descriptor
 *   inhibit - Inhibit state (negative = inhibit)
 *   update_xmit - Update transmit state (negative = yes)
 *
 * Original address: 0x00e1c94a
 */
void SIO_$I_INHIBIT_RCV(sio_desc_t *desc, int8_t inhibit, int8_t update_xmit);

/*
 * SIO_$I_INHIBIT_XMIT - Control transmit inhibit state
 *
 * Controls software flow control for transmit.
 *
 * Parameters:
 *   desc - SIO descriptor
 *   inhibit - Inhibit state (negative = inhibit)
 *
 * Returns:
 *   New transmit state
 *
 * Original address: 0x00e1c9ce
 */
int8_t SIO_$I_INHIBIT_XMIT(sio_desc_t *desc, int8_t inhibit);

/*
 * SIO_$I_GET_DESC - Get SIO descriptor for terminal line
 *
 * Retrieves the SIO descriptor associated with a terminal line.
 *
 * Parameters:
 *   line_num - Terminal line number
 *   status_ret - Status return
 *
 * Returns:
 *   SIO descriptor pointer (in A0 register on m68k)
 *
 * Original address: 0x00e667c6
 */
sio_desc_t *SIO_$I_GET_DESC(int16_t line_num, status_$t *status_ret);

/*
 * SIO_$I_ERR - Get and clear pending receive errors
 *
 * Returns the first pending error and clears it from the
 * pending error mask.
 *
 * Parameters:
 *   desc - SIO descriptor
 *   check_all - Check all errors (negative) or just some
 *
 * Returns:
 *   Status code for the error, or 0 if none
 *
 * Original address: 0x00e67d9c
 */
status_$t SIO_$I_ERR(sio_desc_t *desc, int8_t check_all);

/*
 * ============================================================================
 * SIO Kernel Function Declarations (K_ prefix)
 * ============================================================================
 */

/*
 * SIO_$K_TIMED_BREAK - Send a timed break signal
 *
 * Sends a break signal for the specified duration.
 * Blocks until break completes or quit signal received.
 *
 * Parameters:
 *   line_ptr - Pointer to terminal line number
 *   duration_ptr - Pointer to duration in milliseconds
 *   status_ret - Status return
 *
 * Original address: 0x00e67ee0
 */
void SIO_$K_TIMED_BREAK(int16_t *line_ptr, uint16_t *duration_ptr,
                        status_$t *status_ret);

/*
 * SIO_$K_SIGNAL_WAIT - Wait for modem signal change
 *
 * Waits for specified modem signals to change state.
 * Returns when signal matches or quit signaled.
 *
 * Parameters:
 *   line_ptr - Pointer to terminal line number
 *   signals_ptr - Pointer to signal mask to wait for
 *   status_ret - Status return
 *
 * Returns:
 *   Signals that matched (in registers)
 *
 * Original address: 0x00e67fbe
 */
uint32_t SIO_$K_SIGNAL_WAIT(int16_t *line_ptr, uint32_t *signals_ptr,
                            status_$t *status_ret);

/*
 * SIO_$K_SET_PARAM - Set serial port parameters
 *
 * Sets one or more serial port parameters (baud rate, parity, etc.)
 *
 * Parameters:
 *   line_ptr - Pointer to terminal line number
 *   params - Pointer to new parameters
 *   change_mask_ptr - Pointer to mask of parameters to change
 *   status_ret - Status return
 *
 * Original address: 0x00e680ac
 */
void SIO_$K_SET_PARAM(int16_t *line_ptr, sio_params_t *params,
                      uint32_t *change_mask_ptr, status_$t *status_ret);

/*
 * SIO_$K_INQ_PARAM - Inquire serial port parameters
 *
 * Returns current serial port parameters.
 *
 * Parameters:
 *   line_ptr - Pointer to terminal line number
 *   params_ret - Pointer to receive parameters
 *   mask_ptr - Pointer to mask (passed to driver)
 *   status_ret - Status return
 *
 * Original address: 0x00e6832a
 */
void SIO_$K_INQ_PARAM(int16_t *line_ptr, sio_params_t *params_ret,
                      uint32_t *mask_ptr, status_$t *status_ret);

#endif /* SIO_H */
