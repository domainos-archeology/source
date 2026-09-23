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

#include "misc/string.h"   /* memcpy: the 0x16-byte parameter block copies */

#include "base/base.h"
#include "ec/ec.h"
#include "term/term.h"   /* dtte_t (SIO_$INIT_DTTE, SIO_$INIT_DESC) */
#include "time/time.h"   /* time_queue_elem_t (the embedded delay element) */

/*
 * ============================================================================
 * SIO Status Codes (module 0x36)
 * ============================================================================
 */
/*
 * Names from the SR10.2 status database ("OS / serial I/O"):
 *   360001 invalid option              360006 data carrier detect (dcd) changed
 *   360002 illegal parameter value     360007 clear to send (cts) changed
 *   360003 invalid handle              360008 incompatible speed request
 *   360004 character framing error     360009 input buffer overrun
 *   360005 character parity error      36000a quit while waiting
 * 0x36000B has no entry in the database; SIO_$I_ERR (0x00E67DF2) can still
 * produce it (from a pending bit its own mask clears first).
 */
#define status_$sio_invalid_option          0x00360001
#define status_$sio_invalid_param           0x00360002  /* illegal parameter value */
#define status_$sio_invalid_handle          0x00360003
#define status_$sio_framing_error           0x00360004  /* character framing error */
#define status_$sio_parity_error            0x00360005  /* character parity error */
#define status_$sio_dcd_changed             0x00360006  /* data carrier detect (dcd) changed */
#define status_$sio_cts_changed             0x00360007  /* clear to send (cts) changed */
#define status_$sio_incompatible_speed      0x00360008  /* incompatible speed request */
#define status_$sio_input_overrun           0x00360009  /* input buffer overrun */
#define status_$sio_quit_while_waiting      0x0036000a  /* quit while waiting */
#define status_$sio_code_0b                 0x0036000b  /* not in the SR10.2 database */

/*
 * ============================================================================
 * SIO Constants
 * ============================================================================
 */

/* Transmit buffer special characters */
#define SIO_TSTART_DELAY_MARKER     0xFE    /* Marker for delay sequence */
#define SIO_TSTART_DELAY_CMD        0x00    /* Delay command byte */

/*
 * The interrupt-level routines reach several descriptor fields with byte
 * instructions on the LOW byte of a wider field (big-endian, so bit n of
 * the byte is bit n of the field):
 *
 *   (0x4F,An) low byte of params.flags1  (+0x4C)
 *   (0x53,An) low byte of params.flags2  (+0x50)
 *   (0x57,An) low byte of params.field_08 (+0x54)
 *   (0x67,An) low byte of pending_int    (+0x64)
 *   (0x75,An) low byte of state          (+0x74)
 *
 * The masks below are therefore applied to the whole field.
 */

/* params.flags1 (+0x4C) bit 0: SIO_$I_INHIBIT_RCV clears it to inhibit,
 * sets it to release, then hands the block to set_params with mask 0x20. */
#define SIO_FLAGS1_RCV_ENABLED      0x01

/* params.flags2 (+0x50) */
#define SIO_CTRL_SOFT_FLOW          0x01    /* software flow control (XON/XOFF) */
#define SIO_CTRL_CTS_FLOW           0x02    /* CTS hardware flow control */
#define SIO_CTRL_DCD_HANGUP         0x04    /* call dcd_handler on DCD loss */
#define SIO_CTRL_RECV_ERROR         0x08    /* receive error notification */

/* params.field_08 (+0x54): change-notification enables */
#define SIO_INT_DCD_CHANGE          0x08    /* DCD change notification */
#define SIO_INT_CTS_CHANGE          0x10    /* CTS change notification */

/*
 * pending_int (+0x64): SIO_$I_ERR maps bits 0..5 to status codes in the
 * order framing (bit 1), parity (bit 0), overrun (bit 2), dcd (bit 3), cts
 * (bit 4), 0x36000B (bit 5); SIO_$I_DCD_CHANGE / SIO_$I_CTS_CHANGE set
 * bits 3 / 4.
 */
#define SIO_PEND_PARITY             0x01    /* -> status_$sio_parity_error */
#define SIO_PEND_FRAMING            0x02    /* -> status_$sio_framing_error */
#define SIO_PEND_OVERRUN            0x04    /* -> status_$sio_input_overrun */
#define SIO_PEND_DCD_CHANGED        0x08    /* -> status_$sio_dcd_changed */
#define SIO_PEND_CTS_CHANGED        0x10    /* -> status_$sio_cts_changed */
#define SIO_PEND_BIT5               0x20    /* -> status_$sio_code_0b */
#define SIO_STAT_DCD_NOTIFY         SIO_PEND_DCD_CHANGED
#define SIO_STAT_CTS_NOTIFY         SIO_PEND_CTS_CHANGED
#define SIO_STAT_RECV_ERROR         SIO_PEND_BIT5
#define SIO_ERR_MASK_ALL            0x1F    /* SIO_$I_ERR, check_all < 0 */
#define SIO_ERR_MASK_SIGNALS        0x18    /* SIO_$I_ERR, check_all >= 0 */

/* state (+0x74), low byte (bits 0..7) */
#define SIO_XMIT_ACTIVE             0x01    /* Transmit in progress */
#define SIO_XMIT_CTS_BLOCKED        0x02    /* Blocked by CTS */
#define SIO_XMIT_INHIBITED          0x04    /* Transmit inhibited (XOFF) */
#define SIO_XMIT_DEFER_INHIBIT      0x20    /* Deferred transmit inhibit */
#define SIO_XMIT_DEFER_PENDING      0x40    /* Deferred operation pending */
#define SIO_XMIT_DEFER_COMPLETE     0x80    /* Deferred operation complete */

/*
 * state (+0x74), low byte, bits 3 and 4.  Both lie inside the 0x1F mask
 * SIO_$I_TSTART tests first (0x00E1C7B6 `moveq #0x1f / and.w (0x74,A0)`):
 * sio_$set_break sets and clears bit 3 with `bset.b/bclr.b #3,(0x75,An)`
 * (0x00E67EAE / 0x00E67EB6); SIO_$I_TSTART sets bit 4 with `ori.w #0x10`
 * on the whole word (0x00E1C8A8) and SIO_DELAY_RESTART clears it together
 * with bit 0 (`and.w #0xFFEE`, 0x00E1C69A).  No routine in the image
 * touches the high byte of state.
 */
#define SIO_STATE_BREAK_ACTIVE      0x08    /* Break active (blocks transmit) */
#define SIO_STATE_DELAY_ACTIVE      0x10    /* Delay timer active (blocks transmit) */

/* Parameter change mask bits */
#define SIO_PARAM_BAUD              0x0003  /* Baud rate (bits 0-1) */
#define SIO_PARAM_PARITY            0x0004  /* params.parity    (SIO_$K_SET_PARAM 0x00E68252 tests +0x14) */
#define SIO_PARAM_STOP_BITS         0x0008  /* params.stop_bits (0x00E6827A, +0x12) */
#define SIO_PARAM_CHAR_SIZE         0x0010  /* params.char_size (0x00E682A6, +0x10) */
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
    /* Pascal 1-based: SIO_$I_TSTART fetches a byte with
     * `move.b (0x5,A2,D0w*0x1),D2b` (00e1c826), so element i lives at
     * txbuf+0x05+i and the array itself starts at 0x06. */
    uint8_t     data[1];        /* 0x06: Buffer data, data[i - 1] (variable) */
} sio_txbuf_t;

/* Layout recovered from the disassembly -- see the field comments above. */
_Static_assert(__builtin_offsetof(sio_txbuf_t, read_idx) == 0x00, "sio_txbuf_t.read_idx");
_Static_assert(__builtin_offsetof(sio_txbuf_t, write_idx) == 0x02, "sio_txbuf_t.write_idx");
_Static_assert(__builtin_offsetof(sio_txbuf_t, size) == 0x04, "sio_txbuf_t.size");
_Static_assert(__builtin_offsetof(sio_txbuf_t, data) == 0x06, "sio_txbuf_t.data");

/*
 * ============================================================================
 * SIO Parameter Block Structure
 * ============================================================================
 *
 * Serial port parameters - 0x16 bytes
 * Used with SIO_$K_SET_PARAM and SIO_$K_INQ_PARAM
 */
/* PACKED: m68k aligns 32-bit fields to 2 bytes, so the recovered offsets
 * below are only reproducible on a 4/8-byte-aligning host if the record is
 * packed.  Packing changes no m68k layout. */
typedef struct sio_params {
    uint32_t    flags1;         /* 0x00: Control flags (flow control, etc.) */
    uint32_t    flags2;         /* 0x04: Extended flags (SIO_CTRL_*) */
    uint32_t    break_mask;     /* 0x08: change-notification enables (SIO_INT_*) */
    uint32_t    baud_rate;      /* 0x0C: Baud rate setting */
    int16_t     char_size;      /* 0x10: Character size (0-3) */
    int16_t     stop_bits;      /* 0x12: Stop bits (1-3) */
    int16_t     parity;         /* 0x14: Parity setting (0-3) */
} __attribute__((packed)) sio_params_t;

/* Layout recovered from the disassembly -- see the field comments above. */
_Static_assert(__builtin_offsetof(sio_params_t, flags1) == 0x00, "sio_params_t.flags1");
_Static_assert(__builtin_offsetof(sio_params_t, flags2) == 0x04, "sio_params_t.flags2");
_Static_assert(__builtin_offsetof(sio_params_t, break_mask) == 0x08, "sio_params_t.break_mask");
_Static_assert(__builtin_offsetof(sio_params_t, baud_rate) == 0x0C, "sio_params_t.baud_rate");
_Static_assert(__builtin_offsetof(sio_params_t, char_size) == 0x10, "sio_params_t.char_size");
_Static_assert(__builtin_offsetof(sio_params_t, stop_bits) == 0x12, "sio_params_t.stop_bits");
_Static_assert(__builtin_offsetof(sio_params_t, parity) == 0x14, "sio_params_t.parity");
_Static_assert(sizeof(sio_params_t) == 0x16, "sio_params_t size");

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
    /*
     * 0x08: the descriptor's own 0x1A-byte time-queue element, used for the
     * transmit delay that SIO_$I_TSTART arms.  0x00E1C8C4 "pea (0x8,A0)"
     * passes exactly this address to TIME_$Q_ADD_CALLBACK as its `qelem`
     * argument, so the storage is per-port and not the single global the
     * earlier decompilation invented.
     */
    time_queue_elem_t delay_qelem;  /* 0x08 .. 0x21 */
    uint16_t    reserved_22;    /* 0x22: Reserved */
    m68k_ptr_t  txbuf;          /* 0x24: Transmit buffer pointer */
    m68k_ptr_t  rcv_handler;    /* 0x28: Default receive handler */
    m68k_ptr_t  drain_handler;  /* 0x2C: Buffer drained handler */
    m68k_ptr_t  dcd_handler;    /* 0x30: DCD loss handler */
    m68k_ptr_t  special_rcv;    /* 0x34: Special receive handler */
    m68k_ptr_t  data_rcv;       /* 0x38: Data receive handler */
    m68k_ptr_t  output_char;    /* 0x3C: Output character function */
    m68k_ptr_t  set_params;     /* 0x40: Set parameters function */
    m68k_ptr_t  inq_params;     /* 0x44: Inquire parameters function */
    m68k_ptr_t  set_break;      /* 0x48: driver set-break entry (vtable+0x20;
                                 * the SIO2681 table at 0x00E3517C holds
                                 * SIO2681_$SET_BREAK at 0x00E3519C).  Only
                                 * sio_$set_break calls it, 0x00E67ED0. */

    /* Parameter block - 0x16 bytes */
    sio_params_t params;        /* 0x4C: Current parameters */
    uint16_t    reserved_62;    /* 0x62: Padding */

    uint32_t    pending_int;    /* 0x64: Pending interrupts */
    ec_$eventcount_t ec;        /* 0x68: Event count (12 bytes) */
    uint16_t    state;          /* 0x74: State flags (SIO_XMIT_* and SIO_STATE_*, all in the low byte) */
    uint16_t    reserved_76;    /* 0x76: Reserved */
} sio_desc_t;

/* Remaining documented offsets (bead source-pewa).  Guarded for the same
 * reason as the sizeof check below: the record holds native pointers and an
 * embedded ec_$eventcount_t, so everything past 0x68 shifts on a 64-bit host. */
#if defined(ARCH_M68K)
_Static_assert(__builtin_offsetof(sio_desc_t, context) == 0x00, "sio_desc_t.context");
_Static_assert(__builtin_offsetof(sio_desc_t, owner) == 0x04, "sio_desc_t.owner");
_Static_assert(__builtin_offsetof(sio_desc_t, delay_qelem) == 0x08, "sio_desc_t.delay_qelem");
_Static_assert(__builtin_offsetof(sio_desc_t, reserved_22) == 0x22, "sio_desc_t.reserved_22");
_Static_assert(__builtin_offsetof(sio_desc_t, txbuf) == 0x24, "sio_desc_t.txbuf");
_Static_assert(__builtin_offsetof(sio_desc_t, rcv_handler) == 0x28, "sio_desc_t.rcv_handler");
_Static_assert(__builtin_offsetof(sio_desc_t, drain_handler) == 0x2C, "sio_desc_t.drain_handler");
_Static_assert(__builtin_offsetof(sio_desc_t, dcd_handler) == 0x30, "sio_desc_t.dcd_handler");
_Static_assert(__builtin_offsetof(sio_desc_t, special_rcv) == 0x34, "sio_desc_t.special_rcv");
_Static_assert(__builtin_offsetof(sio_desc_t, data_rcv) == 0x38, "sio_desc_t.data_rcv");
_Static_assert(__builtin_offsetof(sio_desc_t, output_char) == 0x3C, "sio_desc_t.output_char");
_Static_assert(__builtin_offsetof(sio_desc_t, set_params) == 0x40, "sio_desc_t.set_params");
_Static_assert(__builtin_offsetof(sio_desc_t, inq_params) == 0x44, "sio_desc_t.inq_params");
_Static_assert(__builtin_offsetof(sio_desc_t, set_break) == 0x48, "sio_desc_t.set_break");
_Static_assert(__builtin_offsetof(sio_desc_t, params) == 0x4C, "sio_desc_t.params");
_Static_assert(__builtin_offsetof(sio_desc_t, reserved_62) == 0x62, "sio_desc_t.reserved_62");
_Static_assert(__builtin_offsetof(sio_desc_t, pending_int) == 0x64, "sio_desc_t.pending_int");
_Static_assert(__builtin_offsetof(sio_desc_t, ec) == 0x68, "sio_desc_t.ec");
_Static_assert(__builtin_offsetof(sio_desc_t, state) == 0x74, "sio_desc_t.state");
_Static_assert(__builtin_offsetof(sio_desc_t, reserved_76) == 0x76, "sio_desc_t.reserved_76");
#endif

/*
 * Shapes of the handler cells the interrupt-level routines call through
 * (all reached via ARCH_VA_TO_PTR on the m68k_ptr_t field):
 *   data_rcv    (+0x38): word result slot, (owner, word 0)      0x00E1C71C
 *   dcd_handler (+0x30): no result slot, (owner)                 0x00E1C75C
 *   set_params  (+0x40): no result slot, (context, &params, mask, &status)
 *                                                                0x00E1C978
 */
typedef int16_t (*sio_data_rcv_fn_t)(m68k_ptr_t owner, uint8_t ch);
typedef void (*sio_dcd_handler_fn_t)(m68k_ptr_t owner);
typedef void (*sio_set_params_fn_t)(m68k_ptr_t context, sio_params_t *params,
                                    uint32_t change_mask, status_$t *status_ret);
/* inq_params (+0x44): no result slot, (context, params_ret, mask, &status),
 * 0x00E68364.  set_break (+0x48): word result slot that is never read,
 * (context, enable byte), 0x00E67ECA-0x00E67ED4 in sio_$set_break; the
 * SIO2681 entry is SIO2681_$SET_BREAK(channel, int8_t).  rcv_handler (+0x28) has the data_rcv shape (0x00E1C678):
 * the second argument is a BYTE in a word slot - the character in
 * SIO_$I_RCV, a cleared word in the CTS/DCD notifications. */
typedef void (*sio_inq_params_fn_t)(m68k_ptr_t context, sio_params_t *params,
                                    uint32_t mask, status_$t *status_ret);
typedef void (*sio_set_break_fn_t)(m68k_ptr_t context, int8_t enable);

/* Verify structure size (should be 0x78 = 120 bytes).  The record holds
 * pointer fields, so the layout only matches on the 32-bit target; host
 * builds (unit tests) skip the check. */
#if defined(ARCH_M68K)
_Static_assert(sizeof(sio_desc_t) == 0x78, "sio_desc_t must be 120 bytes");
#endif

/*
 * ============================================================================
 * SIO Public Function Declarations
 * ============================================================================
 */

/*
 * Descriptor initialization helpers.  These are called from TERM_$INIT
 * (term/init.c) as well as SIO_$INIT, so they are public.
 */
/*
 * SIO_$INIT_DESC - Initialize an SIO descriptor
 *
 * Populates a full sio_desc_t structure with context, owner, parameter
 * block, handler function pointers, vtable entries, and transmit buffer
 * pointer. Stores a back-pointer into the DTTE, then calls SIO_$I_INIT.
 *
 * Parameters:
 *   desc         - SIO descriptor to initialize
 *   param_block  - Source parameter block (22 bytes)
 *   dtte         - DTTE entry pointer (receives back-pointer at offset 0x28)
 *   owner_ptr    - Pointer to owner handle (dereferenced)
 *   txbuf_ptr    - Pointer to transmit buffer pointer (dereferenced)
 *   handlers     - Array of 4 handler function pointers
 *   context_ptr  - Pointer to context handle (dereferenced)
 *   vtable       - Vtable structure (entries copied from offset 0x14)
 *
 * Original address: 0x00e32ab2
 */
void SIO_$INIT_DESC(sio_desc_t *desc, void *param_block, void *dtte,
                    m68k_ptr_t *owner_ptr, m68k_ptr_t *txbuf_ptr,
                    m68k_ptr_t *handlers, m68k_ptr_t *context_ptr,
                    char *vtable);

/*
 * SIO_$INIT_DTTE - Initialize a DTTE (Display Terminal Table Entry)
 *
 * Initializes three inline event counts at offsets 0x00, 0x0C, and 0x18,
 * sets the discipline field, and clears the flags byte.
 *
 * Parameters:
 *   dtte       - Pointer to DTTE entry to initialize
 *   discipline - Terminal discipline value (0=TTY, 2=console, etc.)
 *
 * Original address: 0x00e32b76
 */
void SIO_$INIT_DTTE(dtte_t *dtte, int16_t discipline);

/*
 * SIO_$INIT_DRAIN_HANDLER - Initialize output buffer drain handler
 *
 * Sets up a 3-word handler record: function pointer, context, and data.
 * The function pointer is set to TTY_$I_OUTPUT_BUFFER_DRAINED.
 *
 * Parameters:
 *   handler     - Pointer to 3-word handler record
 *   dtte        - DTTE pointer (unused, part of calling convention)
 *   data_ptr    - Pointer to data pointer value (stored at handler[2])
 *   context_ptr - Pointer to context pointer value (stored at handler[1])
 *
 * Original address: 0x00e32bb8
 */
void SIO_$INIT_DRAIN_HANDLER(m68k_ptr_t *handler, void *dtte,
                              m68k_ptr_t *data_ptr, m68k_ptr_t *context_ptr);

/*
 * SIO_$INIT_LINE - Initialize SIO TTY line descriptor
 *
 * Sets up a TTY descriptor from SIO port configuration data. Copies the
 * line identifier (from *config), hardware register addresses (from hw_info),
 * I/O buffer pointers (from port_data), then calls TTY_$I_INIT.
 *
 * Parameters:
 *   desc      - TTY descriptor to initialize
 *   port_data - SIO port data structure (DTTE)
 *   config    - Pointer to m68k_ptr_t value used as line ID (dereferenced once)
 *   hw_info   - Hardware info block (register addresses at +0x08)
 *
 * Original address: 0x00e32b26
 */
void SIO_$INIT_LINE(void *desc, void *port_data, m68k_ptr_t *config, void *hw_info);


/*
 * SIO_$INIT - Initialize a serial I/O port
 *
 * Initializes an SIO descriptor for the specified port.
 * Port 1 is the console (KBD/display handlers), other ports are
 * generic serial (TTY handlers).
 *
 * For console (port 1):
 *   - Calls OS_TERM_INIT to set up keyboard/display terminal structure
 *   - Calls SIO_$INIT_LINE with console hardware info
 *   - Calls SIO_$INIT_DRAIN_HANDLER for drain notification
 *   - Calls SIO_$INIT_DESC with KBD handler array and console params
 *   - Calls SIO_$INIT_DTTE with discipline=2 (console)
 *
 * For generic serial:
 *   - Calls SIO_$INIT_LINE with generic hardware info
 *   - Calls SIO_$INIT_DESC with TTY handler array and generic params
 *   - Calls SIO_$INIT_DTTE with discipline=0 (serial)
 *   - If flags < 0: enables crash handler (ESC key = 0x1B)
 *   - If flags >= 0: calls driver's set_params to apply initial config
 *
 * Parameters:
 *   port_num    - Port number (1 = console, others = generic serial)
 *   context_ptr - Pointer to context handle for SIO descriptor (passed to INIT_DESC)
 *   vtable_ptr  - Pointer to vtable structure with function ptrs at +0x14 (passed to INIT_DESC)
 *   desc_ret    - Pointer to receive SIO descriptor address
 *   flags       - Initialization flags (bit 7 set = enable crash handler on generic port)
 *   status_ret  - Status return
 *
 * Original address: 0x00e32be0
 */
void SIO_$INIT(int16_t port_num, void *context_ptr, void *vtable_ptr,
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
 *   Domain boolean (`sne D0b` at 0x00E1C6D0): true if a transmission is
 *   active after the restart
 *
 * Original address: 0x00e1c6b4
 */
boolean SIO_$I_XMIT_DONE(sio_desc_t *desc);

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
 * A Pascal PROCEDURE: none of the eight call sites (0x00E1C6A8, 0x00E1C6C6,
 * 0x00E1C6F2, 0x00E1C76E, 0x00E1C9C0, 0x00E1C9EE, 0x00E1D152 and the one in
 * SIO2681_$SET_BREAK) reserves a result slot, so there is no return value.
 *
 * Original address: 0x00e1c7a8
 */
void SIO_$I_TSTART(sio_desc_t *desc);

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
 * A procedure: D0 is left holding whatever SIO_$I_TSTART or the argument
 * byte left in it, and the only reference to the routine is the pointer
 * cell at 0x00E2CA3C in TERM_$DATA.
 *
 * Original address: 0x00e1c9ce (44 bytes)
 */
void SIO_$I_INHIBIT_XMIT(sio_desc_t *desc, int8_t inhibit);

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
 *   SIO descriptor pointer in A0.  On both failure paths (real-line lookup
 *   failed, or no descriptor for the line) the image returns the frame slot
 *   (-0x4,A6) without ever having written it; the C returns NULL there.
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
 * SIO_$K_SIGNAL_WAIT - Wait until a modem signal in *signals_ptr is up
 *
 * Loops: the driver's inq_params refreshes the DESCRIPTOR's own parameter
 * block (0x00E68042 pushes desc+0x4C) with mask 0x180, and the loop ends
 * when params.flags1 & *signals_ptr is non-zero; otherwise EC_$WAITN on
 * the descriptor's eventcount and the quit eventcount, and a quit ends
 * it with status_$sio_quit_while_waiting.
 *
 * A procedure reached only through SVC_$TRAP0_TABLE[0x7C]: no caller
 * reserves a result slot and D0 is not set on every path (it holds
 * SIO_$I_GET_DESC's A0 copy or the last `and.l`), so no value is
 * returned.
 *
 * Original address: 0x00e67fbe (238 bytes)
 */
void SIO_$K_SIGNAL_WAIT(int16_t *line_ptr, uint32_t *signals_ptr,
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
                      const uint32_t *change_mask_ptr, status_$t *status_ret);

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
                      const uint32_t *mask_ptr, status_$t *status_ret);

#endif /* SIO_H */
