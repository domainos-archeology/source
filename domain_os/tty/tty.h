// TTY (Teletype) subsystem header for Domain/OS
// Provides terminal line discipline handling

#ifndef TTY_H
#define TTY_H

#include "base/base.h"
#include "ml/ml.h"

// =============================================================================
// TTY Constants
// =============================================================================

// TTY subsystem lock ID (used with ML_$LOCK/ML_$UNLOCK)
#define TTY_LOCK_ID 3

// Input/output buffer size (circular buffer with 256 entries, indices 1-256)
#define TTY_BUFFER_SIZE 0x100

// Maximum number of function characters
#define TTY_MAX_FUNC_CHARS 0x12

// Character classes for function character mapping
#define TTY_CHAR_CLASS_SIGINT 0x00   // Interrupt (^C)
#define TTY_CHAR_CLASS_SIGQUIT 0x01  // Quit (^\)
#define TTY_CHAR_CLASS_SIGTSTP 0x02  // Suspend (^Z)
#define TTY_CHAR_CLASS_BREAK 0x03    // Break character (end of line)
#define TTY_CHAR_CLASS_EOF 0x04      // End of file (^D)
// Classes 0x05/0x06 resolved from the two default tables (source-2qng):
//   TTY_$I_INIT (0xE33284) copies the default function characters from
//   0x00E351D8; TTY_$I_SET_DFL_FUNCS (0xE67274) sets A5 = 0x00E8242C, whose
//   first 18 words are the function-index -> character-class table.
//   Function index 13: character 0x13 (^S, DC3 = XOFF) -> class 0x05
//   Function index 14: character 0x11 (^Q, DC1 = XON)  -> class 0x06
// and the handlers agree: class 0x05 (0xE1BA42) SETS the output-stop bit and
// calls the xon/xoff handler with TRUE; class 0x06 (0xE1BA60) CLEARS it, calls
// the handler with FALSE and advances the output eventcount.
#define TTY_CHAR_CLASS_XOFF 0x05     // ^S/DC3: stop output - sets TTY_STATUS_OUTPUT_STOPPED
#define TTY_CHAR_CLASS_XON 0x06      // ^Q/DC1: resume output - clears TTY_STATUS_OUTPUT_STOPPED
#define TTY_CHAR_CLASS_DEL 0x07      // Delete character
#define TTY_CHAR_CLASS_KILL 0x08     // Kill line   (dispatches TTY_$I_KILL_LINE, 0xE1BAF6)
#define TTY_CHAR_CLASS_WERASE 0x09   // Word erase  (dispatches TTY_$I_WORD_ERASE, 0xE1BAE2)
#define TTY_CHAR_CLASS_REPRINT 0x0A  // Reprint line
#define TTY_CHAR_CLASS_NL 0x0B       // Newline
#define TTY_CHAR_CLASS_DISCARD 0x0C  // Discard output (^O)
#define TTY_CHAR_CLASS_FLUSHOUT 0x0D // Flush output
#define TTY_CHAR_CLASS_CR 0x0E       // Carriage return
#define TTY_CHAR_CLASS_CRLF 0x0F     // CR/LF handling
#define TTY_CHAR_CLASS_TAB 0x10      // Tab character
#define TTY_CHAR_CLASS_CRASH 0x11    // Crash system (debug)
#define TTY_CHAR_CLASS_NORMAL 0x12   // Normal character (no special handling)

// Signal numbers used with TTY_$I_SIGNAL
#define TTY_SIG_HUP 0x01   // Hangup
#define TTY_SIG_INT 0x02   // Interrupt
#define TTY_SIG_QUIT 0x03  // Quit
#define TTY_SIG_TSTP 0x15  // Terminal stop (^Z)
#define TTY_SIG_WINCH 0x1A // Window size change
#define TTY_SIG_CONT 0x16  // Continue

// =============================================================================
// TTY State Flags (offset 0x08-0x09 in tty_desc_t)
// =============================================================================
#define TTY_FLAG_PARITY_ERR 0x0080 // Parity error on current char
#define TTY_FLAG_RAW_MODE 0x0040   // Raw mode (no char class processing)

// =============================================================================
// TTY Status Flags (low byte of state_flags at offset 0x08)
// Assembly accesses byte 0x09 (LSB of state_flags on big-endian)
// =============================================================================
#define TTY_STATUS_OUTPUT_WAIT 0x01  // Waiting for output buffer drain
#define TTY_STATUS_INPUT_WAIT 0x02   // Waiting for input
#define TTY_STATUS_OUTPUT_STOPPED 0x04 // Output stopped by XOFF (^S); tty_$i_put_chars
                                     // returns 0 while set (0xE1B032)
#define TTY_STATUS_SIG_PEND 0x10     // Signal pending on input
#define TTY_STATUS_OUTPUT_FLUSH 0x20 // Output flush in progress
#define TTY_STATUS_EOF_PEND 0x40     // EOF pending

// =============================================================================
// TTY Error Flags (low byte of pending_signal at offset 0x0A)
// Assembly accesses byte 0x0B (LSB of pending_signal on big-endian)
// =============================================================================
#define TTY_ERR_CALLBACK 0x01 // Error callback set
#define TTY_ERR_OVERFLOW 0x02 // Input buffer overflow

// =============================================================================
// Callback signatures stored in tty_desc_t
//
// Both are called with the full 32-bit line_id (move.l (A2),-(SP)) and one or
// two Domain booleans (0xFF / 0x00) pushed in 2-byte slots.
// =============================================================================

// xon_xoff_handler (0x2B8): 0xE1BA50..0xE1BA5A, 0xE1BA6C..0xE1BA76,
// TTY_$K_RESET 0xE67374
typedef void (*tty_xon_xoff_handler_t)(uint32_t line_id, boolean stop);

// flow_ctrl_handler (0x2BC): 0xE1BC76..0xE1BC88, TTY_$I_FLUSH_INPUT 0xE1B7EA,
// TTY_$K_RESET 0xE67388, TTY_$K_GET 0xE1C54E
typedef void (*tty_flow_ctrl_handler_t)(uint32_t line_id, boolean assert_flow,
                                        boolean use_hw_flow);

// =============================================================================
// TTY Callback Descriptor
// Each TTY has up to 6 signal callback entries (12 bytes each)
// =============================================================================
typedef struct tty_signal_entry {
  m68k_ptr_t tty_desc; // 0x00: Back pointer to TTY descriptor
  m68k_ptr_t callback; // 0x04: Callback function pointer
  uint16_t signal_num; // 0x08: Signal number
  uint16_t reserved;   // 0x0A: Reserved/padding
} tty_signal_entry_t;

/* Remaining documented offsets (bead source-pewa). */
_Static_assert(__builtin_offsetof(tty_signal_entry_t, tty_desc) == 0x00, "tty_signal_entry_t.tty_desc");
_Static_assert(__builtin_offsetof(tty_signal_entry_t, callback) == 0x04, "tty_signal_entry_t.callback");
_Static_assert(__builtin_offsetof(tty_signal_entry_t, signal_num) == 0x08, "tty_signal_entry_t.signal_num");
_Static_assert(__builtin_offsetof(tty_signal_entry_t, reserved) == 0x0A, "tty_signal_entry_t.reserved");

_Static_assert(sizeof(tty_signal_entry_t) == 0x0C,
               "tty_signal_entry_t must be 12 bytes");

// =============================================================================
// TTY Descriptor Structure
// Main control structure for a TTY line (approx 0x4DC bytes)
// =============================================================================
typedef struct tty_desc {
  // Basic identification and state (0x00-0x0F)
  uint32_t line_id;        // 0x00: Terminal line identifier
  m68k_ptr_t handler_ptr;  // 0x04: Handler structure pointer
  uint16_t state_flags;    // 0x08: State/status flags (TTY_STATUS_* in low byte)
  uint16_t pending_signal; // 0x0A: Pending signal number (TTY_ERR_* in low byte)
  uint32_t output_flags;   // 0x0C: Output control flags (32-bit word, assembly: move.l (0xC,A0))

  // Mode flags (0x10-0x1F)
  uint32_t reserved_10; // 0x10: Reserved
  uint32_t input_flags; // 0x14: Input processing flags
  uint16_t reserved_18; // 0x18: Reserved
  uint16_t reserved_1A; // 0x1A: Reserved
  uint32_t echo_flags;  // 0x1C: Echo control flags

  // Function character control (0x20-0x3F)
  uint32_t func_enabled; // 0x20: Bitmask of enabled function chars
  uint8_t func_chars[TTY_MAX_FUNC_CHARS]; // 0x24: Function character bindings
  uint16_t reserved_36;                   // 0x36: Reserved/padding

  // Input break mode (0x38-0x3F)
  uint16_t break_mode;  // 0x38: Input break mode (0=raw, 1-3=line modes)
  uint16_t min_chars;   // 0x3A: Minimum chars before signaling reader
  uint32_t reserved_3C; // 0x3C: Reserved

  // Delay settings (0x40-0x4F)
  uint16_t delay[5];    // 0x40: Delay settings for various operations
  uint16_t reserved_4A; // 0x4A: Reserved

  // Process group ownership (0x4C-0x5B)
  uid_t pgroup_uid;             // 0x4C: Process group UID (8 bytes)
  uint16_t session_id;          // 0x54: Session ID
  uint16_t saved_input_flags;   // 0x56: Saved input flags state
  uint16_t column;              // 0x58: Display column position
  uint16_t reserved_5A;         // 0x5A: Reserved

  // Signal callback entries (0x5C-0xA3) - 6 entries of 12 bytes each
  tty_signal_entry_t signals[6]; // 0x5C: Signal callback entries

  // Character class table (0xA4-0x2A3) - 256 entries of 2 bytes each
  uint16_t char_class[256]; // 0xA4: Character class for each byte value

  // Handler function pointers (0x2A4-0x2C3)
  m68k_ptr_t input_ec;          // 0x2A4: Input eventcount pointer
  m68k_ptr_t output_ec;         // 0x2A8: Output eventcount pointer
  m68k_ptr_t reserved_2AC;      // 0x2AC: Reserved
  m68k_ptr_t err_handler;       // 0x2B0: Error handler function
  m68k_ptr_t xmit_callback;     // 0x2B4: Transmit callback function
  tty_xon_xoff_handler_t xon_xoff_handler;   // 0x2B8: XON/XOFF handler
  tty_flow_ctrl_handler_t flow_ctrl_handler; // 0x2BC: Flow control handler
  m68k_ptr_t status_handler;    // 0x2C0: Status change handler

  // Timestamp of the last completed input line.  TIME_$CLOCK writes a 48-bit
  // clock_t here (0x2C4..0x2C9); see TTY_$I_RCV 0xE1BCE4 and
  // TTY_$I_STORE_PARITY 0xE1BD8C.  Split into two scalars rather than an
  // embedded clock_t so the layout is identical on m68k (2-byte alignment)
  // and on the host test build (4-byte alignment).
  uint32_t last_input_clock_high; // 0x2C4: clock_t.high
  uint16_t last_input_clock_low;  // 0x2C8: clock_t.low

  // ---------------------------------------------------------------------
  // Input circular buffer (0x2CA..0x3D1).
  //
  // tty_$i_buf_insert (0x00E1AF0A) is handed &input_read, i.e. the record
  //   { head: word; tail: word; size: word; data: array[1..256] of char }
  // and stores at (0x5,A0,tail.w) == data[tail-1].  In TTY_$I_RCV the same
  // element is reached as (0x2d1,A2,tail.w), so input_buffer[] itself starts
  // at 0x2D2 and the assembly's 0x2D1 displacement is &input_buffer[-1].
  // ---------------------------------------------------------------------
  uint16_t input_head; // 0x2CA: end of committed input (consumer limit), 1..256
  uint16_t input_read; // 0x2CC: buffer header word 0 ("head" seen by buf_insert)
  uint16_t input_tail; // 0x2CE: buffer header word 1 (write position), 1..256
  uint16_t input_size; // 0x2D0: buffer size, always TTY_BUFFER_SIZE (0xE33340)

  uint8_t input_buffer[TTY_BUFFER_SIZE]; // 0x2D2: data, indexed [pos - 1]

  // ---------------------------------------------------------------------
  // Output circular buffer (0x3D2..0x4D7), same record shape: the header
  // handed to tty_$i_buf_put is &output_head, so output_read is the write
  // position and output_tail is really the size word (0xE33352).
  // ---------------------------------------------------------------------
  uint16_t output_head;  // 0x3D2: buffer header word 0 (consumer position)
  uint16_t output_read;  // 0x3D4: buffer header word 1 (write position)
  uint16_t output_tail;  // 0x3D6: buffer size word, always TTY_BUFFER_SIZE

  // Output buffer (0x3D8-0x4D7) - 256 bytes
  uint8_t output_buffer[TTY_BUFFER_SIZE]; // 0x3D8: data, indexed [pos - 1]

  // Crash/debug settings (0x4D8-0x4DB)
  uint8_t crash_char;    // 0x4D8: Crash character (if enabled)
  uint8_t raw_mode;      // 0x4D9: Raw mode flag (nonzero = raw)
  uint16_t reserved_4DA; // 0x4DA: Reserved

} tty_desc_t;

/* Remaining documented offsets (bead source-pewa). */
#if defined(ARCH_M68K)
_Static_assert(__builtin_offsetof(tty_desc_t, line_id) == 0x00, "tty_desc_t.line_id");
_Static_assert(__builtin_offsetof(tty_desc_t, handler_ptr) == 0x04, "tty_desc_t.handler_ptr");
_Static_assert(__builtin_offsetof(tty_desc_t, output_flags) == 0x0C, "tty_desc_t.output_flags");
_Static_assert(__builtin_offsetof(tty_desc_t, reserved_10) == 0x10, "tty_desc_t.reserved_10");
_Static_assert(__builtin_offsetof(tty_desc_t, reserved_18) == 0x18, "tty_desc_t.reserved_18");
_Static_assert(__builtin_offsetof(tty_desc_t, reserved_1A) == 0x1A, "tty_desc_t.reserved_1A");
_Static_assert(__builtin_offsetof(tty_desc_t, echo_flags) == 0x1C, "tty_desc_t.echo_flags");
_Static_assert(__builtin_offsetof(tty_desc_t, func_enabled) == 0x20, "tty_desc_t.func_enabled");
_Static_assert(__builtin_offsetof(tty_desc_t, func_chars) == 0x24, "tty_desc_t.func_chars");
_Static_assert(__builtin_offsetof(tty_desc_t, reserved_36) == 0x36, "tty_desc_t.reserved_36");
_Static_assert(__builtin_offsetof(tty_desc_t, min_chars) == 0x3A, "tty_desc_t.min_chars");
_Static_assert(__builtin_offsetof(tty_desc_t, reserved_3C) == 0x3C, "tty_desc_t.reserved_3C");
_Static_assert(__builtin_offsetof(tty_desc_t, delay) == 0x40, "tty_desc_t.delay");
_Static_assert(__builtin_offsetof(tty_desc_t, reserved_4A) == 0x4A, "tty_desc_t.reserved_4A");
_Static_assert(__builtin_offsetof(tty_desc_t, pgroup_uid) == 0x4C, "tty_desc_t.pgroup_uid");
_Static_assert(__builtin_offsetof(tty_desc_t, session_id) == 0x54, "tty_desc_t.session_id");
_Static_assert(__builtin_offsetof(tty_desc_t, reserved_5A) == 0x5A, "tty_desc_t.reserved_5A");
_Static_assert(__builtin_offsetof(tty_desc_t, reserved_2AC) == 0x2AC, "tty_desc_t.reserved_2AC");
_Static_assert(__builtin_offsetof(tty_desc_t, err_handler) == 0x2B0, "tty_desc_t.err_handler");
_Static_assert(__builtin_offsetof(tty_desc_t, xmit_callback) == 0x2B4, "tty_desc_t.xmit_callback");
_Static_assert(__builtin_offsetof(tty_desc_t, status_handler) == 0x2C0, "tty_desc_t.status_handler");
_Static_assert(__builtin_offsetof(tty_desc_t, last_input_clock_low) == 0x2C8, "tty_desc_t.last_input_clock_low");
_Static_assert(__builtin_offsetof(tty_desc_t, reserved_4DA) == 0x4DA, "tty_desc_t.reserved_4DA");
#endif

// Layout recovered from the SAU2 image; see the addresses cited above.
#if defined(ARCH_M68K)
_Static_assert(__builtin_offsetof(tty_desc_t, state_flags) == 0x08, "tty_desc_t.state_flags");
_Static_assert(__builtin_offsetof(tty_desc_t, pending_signal) == 0x0A, "tty_desc_t.pending_signal");
_Static_assert(__builtin_offsetof(tty_desc_t, input_flags) == 0x14, "tty_desc_t.input_flags");
_Static_assert(__builtin_offsetof(tty_desc_t, break_mode) == 0x38, "tty_desc_t.break_mode");
_Static_assert(__builtin_offsetof(tty_desc_t, saved_input_flags) == 0x56, "tty_desc_t.saved_input_flags");
_Static_assert(__builtin_offsetof(tty_desc_t, column) == 0x58, "tty_desc_t.column");
_Static_assert(__builtin_offsetof(tty_desc_t, signals) == 0x5C, "tty_desc_t.signals");
_Static_assert(__builtin_offsetof(tty_desc_t, char_class) == 0xA4, "tty_desc_t.char_class");
_Static_assert(__builtin_offsetof(tty_desc_t, input_ec) == 0x2A4, "tty_desc_t.input_ec");
_Static_assert(__builtin_offsetof(tty_desc_t, output_ec) == 0x2A8, "tty_desc_t.output_ec");
_Static_assert(__builtin_offsetof(tty_desc_t, xon_xoff_handler) == 0x2B8, "tty_desc_t.xon_xoff_handler");
_Static_assert(__builtin_offsetof(tty_desc_t, flow_ctrl_handler) == 0x2BC, "tty_desc_t.flow_ctrl_handler");
_Static_assert(__builtin_offsetof(tty_desc_t, last_input_clock_high) == 0x2C4, "tty_desc_t.last_input_clock_high");
_Static_assert(__builtin_offsetof(tty_desc_t, input_head) == 0x2CA, "tty_desc_t.input_head");
_Static_assert(__builtin_offsetof(tty_desc_t, input_read) == 0x2CC, "tty_desc_t.input_read");
_Static_assert(__builtin_offsetof(tty_desc_t, input_tail) == 0x2CE, "tty_desc_t.input_tail");
_Static_assert(__builtin_offsetof(tty_desc_t, input_size) == 0x2D0, "tty_desc_t.input_size");
_Static_assert(__builtin_offsetof(tty_desc_t, input_buffer) == 0x2D2, "tty_desc_t.input_buffer");
_Static_assert(__builtin_offsetof(tty_desc_t, output_head) == 0x3D2, "tty_desc_t.output_head");
_Static_assert(__builtin_offsetof(tty_desc_t, output_read) == 0x3D4, "tty_desc_t.output_read");
_Static_assert(__builtin_offsetof(tty_desc_t, output_tail) == 0x3D6, "tty_desc_t.output_tail");
_Static_assert(__builtin_offsetof(tty_desc_t, output_buffer) == 0x3D8, "tty_desc_t.output_buffer");
_Static_assert(__builtin_offsetof(tty_desc_t, crash_char) == 0x4D8, "tty_desc_t.crash_char");
_Static_assert(__builtin_offsetof(tty_desc_t, raw_mode) == 0x4D9, "tty_desc_t.raw_mode");
_Static_assert(sizeof(tty_desc_t) == 0x4DC, "tty_desc_t size");
#endif

// =============================================================================
// Global TTY data
// =============================================================================
extern uint32_t TTY_$SPIN_LOCK; // Spin lock for TTY operations (at 0xe2dd74)

// =============================================================================
// Internal TTY functions (TTY_$I_* - interrupt/internal level)
// =============================================================================

// TTY_$I_INIT - Initialize a TTY descriptor structure
// @param tty: Pointer to TTY descriptor to initialize
extern void TTY_$I_INIT(tty_desc_t *tty);

// TTY_$I_GET_DESC - Get TTY descriptor for a terminal line
// @param line: Terminal line number
// @param status: Pointer to receive status code
// @return: Pointer to TTY descriptor (returned via A0 register)
extern tty_desc_t *TTY_$I_GET_DESC(short line, status_$t *status);

// TTY_$I_RCV - Receive a character (interrupt level)
// @param tty: TTY descriptor
// @param ch: Character received
extern void TTY_$I_RCV(tty_desc_t *tty, uint8_t ch);

// TTY_$I_SIGNAL - Send a signal to the TTY's process group
// @param tty: TTY descriptor
// @param signal: Signal number (TTY_SIG_*)
extern void TTY_$I_SIGNAL(tty_desc_t *tty, short signal);

// TTY_$I_FLUSH_INPUT - Flush the input buffer
// @param tty: TTY descriptor
extern void TTY_$I_FLUSH_INPUT(tty_desc_t *tty);

// TTY_$I_FLUSH_OUTPUT - Flush the output buffer
// @param tty: TTY descriptor
extern void TTY_$I_FLUSH_OUTPUT(tty_desc_t *tty);

// TTY_$I_OUTPUT_BUFFER_DRAINED - Called when output buffer is empty
// @param tty: TTY descriptor
extern void TTY_$I_OUTPUT_BUFFER_DRAINED(tty_desc_t *tty);

// TTY_$I_ERR - Handle TTY error condition
// @param tty: TTY descriptor
// @param fatal: Nonzero if error is fatal
extern void TTY_$I_ERR(tty_desc_t *tty, char fatal);

// TTY_$I_INTERRUPT - Handle interrupt (^C)
// @param tty: TTY descriptor
extern void TTY_$I_INTERRUPT(tty_desc_t *tty);

// TTY_$I_HUP - Handle hangup
// @param tty: TTY descriptor
extern void TTY_$I_HUP(tty_desc_t *tty);

// TTY_$I_DXM_SIGNAL - DXM callback for signal delivery
// @param entry: Signal entry pointer
extern void TTY_$I_DXM_SIGNAL(tty_signal_entry_t **entry);

// TTY_$I_SET_RAW - Set raw mode
// @param line: Terminal line number
// @param raw: Nonzero for raw mode
// @param status: Pointer to receive status code
extern void TTY_$I_SET_RAW(short line, char raw, status_$t *status);

// TTY_$I_INQ_RAW - Inquire raw mode setting
// @param line: Terminal line number
// @param raw: Pointer to receive raw mode flag
// @param status: Pointer to receive status code
extern void TTY_$I_INQ_RAW(short line, char *raw, status_$t *status);

// TTY_$I_ENABLE_CRASH_FUNC - Enable/disable crash character
// @param tty: TTY descriptor
// @param ch: Character to use for crash
// @param enable: Nonzero to enable
extern void TTY_$I_ENABLE_CRASH_FUNC(tty_desc_t *tty, uint8_t ch, char enable);

// =============================================================================
// Kernel-level TTY functions (TTY_$K_* - kernel interface)
// =============================================================================

// TTY_$K_GET - Read characters from TTY
// @param line_ptr: Pointer to terminal line number
// @param options: Pointer to read options structure (2-byte flags)
// @param buffer: Buffer to receive characters
// @param count: Pointer to max count (updated with actual count)
// @param status: Pointer to receive status code
// @return: Number of characters read
extern ushort TTY_$K_GET(short *line_ptr, void *options, void *buffer,
                         ushort *count, status_$t *status);

// TTY_$K_PUT - Write characters to TTY
// @param line_ptr: Pointer to terminal line number
// @param options: Pointer to write options structure (2-byte flags)
// @param buffer: Buffer containing characters
// @param count: Pointer to count of characters (updated on return)
// @param status: Pointer to receive status code
extern void TTY_$K_PUT(short *line_ptr, void *options, void *buffer,
                       ushort *count, status_$t *status);

// TTY_$K_FLUSH_INPUT - Flush input buffer (kernel level)
// @param line_ptr: Pointer to terminal line number
// @param status: Pointer to receive status code
extern void TTY_$K_FLUSH_INPUT(short *line_ptr, status_$t *status);

// TTY_$K_FLUSH_OUTPUT - Flush output buffer (kernel level)
// @param line_ptr: Pointer to terminal line number
// @param status: Pointer to receive status code
extern void TTY_$K_FLUSH_OUTPUT(short *line_ptr, status_$t *status);

// TTY_$K_SIMULATE_TERMINAL_INPUT - Simulate input character
// @param line_ptr: Pointer to terminal line number
// @param ch_ptr: Pointer to character to simulate
// @param status: Pointer to receive status code
extern void TTY_$K_SIMULATE_TERMINAL_INPUT(short *line_ptr, char *ch_ptr,
                                           status_$t *status);

// TTY_$K_RESET - Reset TTY to default settings
// @param line_ptr: Pointer to terminal line number
// @param status: Pointer to receive status code
extern void TTY_$K_RESET(short *line_ptr, status_$t *status);

// TTY_$K_SET_FLAG - Set a TTY flag
// @param line_ptr: Pointer to terminal line number
// @param flag_ptr: Pointer to flag number
// @param value_ptr: Pointer to value (nonzero = set)
// @param status: Pointer to receive status code
extern void TTY_$K_SET_FLAG(short *line_ptr, short *flag_ptr, char *value_ptr,
                            status_$t *status);

// TTY_$K_INQ_FLAGS - Inquire TTY flags
// @param line_ptr: Pointer to terminal line number
// @param flags_ptr: Pointer to receive flags
// @param status: Pointer to receive status code
extern void TTY_$K_INQ_FLAGS(short *line_ptr, uint16_t *flags_ptr,
                             status_$t *status);

// TTY_$K_SET_FUNC_CHAR - Set function character binding
// @param line_ptr: Pointer to terminal line number
// @param func_ptr: Pointer to function number (0-17)
// @param ch_ptr: Pointer to character value
// @param status: Pointer to receive status code
extern void TTY_$K_SET_FUNC_CHAR(short *line_ptr, const uint16_t *func_ptr,
                                 const char *ch_ptr, status_$t *status);

// TTY_$K_INQ_FUNC_CHAR - Inquire function character binding
// @param line_ptr: Pointer to terminal line number
// @param func_ptr: Pointer to function number (0-17)
// @param ch_ptr: Pointer to receive character value
// @param status: Pointer to receive status code
extern void TTY_$K_INQ_FUNC_CHAR(short *line_ptr, const uint16_t *func_ptr,
                                 char *ch_ptr, status_$t *status);

// TTY_$K_ENABLE_FUNC - Enable/disable function character
// @param line_ptr: Pointer to terminal line number
// @param func_ptr: Pointer to function number (0-17)
// @param enable_ptr: Pointer to enable flag (negative = enable, zero/positive = disable)
// @param status: Pointer to receive status code
extern void TTY_$K_ENABLE_FUNC(short *line_ptr, const uint16_t *func_ptr,
                               const char *enable_ptr, status_$t *status);

// TTY_$K_INQ_FUNC_ENABLED - Inquire enabled function characters
// @param line_ptr: Pointer to terminal line number
// @param enabled_ptr: Pointer to receive enabled bitmask
// @param status: Pointer to receive status code
extern void TTY_$K_INQ_FUNC_ENABLED(short *line_ptr, uint32_t *enabled_ptr,
                                    status_$t *status);

// TTY_$K_SET_INPUT_FLAG - Set input processing flag
// @param line_ptr: Pointer to terminal line number
// @param flag_ptr: Pointer to flag number (bit position)
// @param value_ptr: Pointer to value (negative = set, zero/positive = clear)
// @param status: Pointer to receive status code
extern void TTY_$K_SET_INPUT_FLAG(short *line_ptr, const uint16_t *flag_ptr,
                                  const char *value_ptr, status_$t *status);

// TTY_$K_INQ_INPUT_FLAGS - Inquire input processing flags
// @param line_ptr: Pointer to terminal line number
// @param flags_ptr: Pointer to receive flags
// @param status: Pointer to receive status code
extern void TTY_$K_INQ_INPUT_FLAGS(short *line_ptr, uint32_t *flags_ptr,
                                   status_$t *status);

// TTY_$K_SET_OUTPUT_FLAG - Set output processing flag
// @param line_ptr: Pointer to terminal line number
// @param flag_ptr: Pointer to flag number (bit position)
// @param value_ptr: Pointer to value (negative = set, zero/positive = clear)
// @param status: Pointer to receive status code
extern void TTY_$K_SET_OUTPUT_FLAG(short *line_ptr, const uint16_t *flag_ptr,
                                   const char *value_ptr, status_$t *status);

// TTY_$K_INQ_OUTPUT_FLAGS - Inquire output processing flags
// @param line_ptr: Pointer to terminal line number
// @param flags_ptr: Pointer to receive flags
// @param status: Pointer to receive status code
extern void TTY_$K_INQ_OUTPUT_FLAGS(short *line_ptr, uint32_t *flags_ptr,
                                    status_$t *status);

// TTY_$K_SET_ECHO_FLAG - Set echo flag
// @param line_ptr: Pointer to terminal line number
// @param flag_ptr: Pointer to flag number
// @param value_ptr: Pointer to value (nonzero = set)
// @param status: Pointer to receive status code
extern void TTY_$K_SET_ECHO_FLAG(short *line_ptr, ushort *flag_ptr,
                                 char *value_ptr, status_$t *status);

// TTY_$K_INQ_ECHO_FLAGS - Inquire echo flags
// @param line_ptr: Pointer to terminal line number
// @param flags_ptr: Pointer to receive flags
// @param status: Pointer to receive status code
extern void TTY_$K_INQ_ECHO_FLAGS(short *line_ptr, uint32_t *flags_ptr,
                                  status_$t *status);

// TTY_$K_SET_INPUT_BREAK_MODE - Set input break mode
// @param line_ptr: Pointer to terminal line number
// @param mode_ptr: Pointer to break mode structure (8 bytes)
// @param status: Pointer to receive status code
extern void TTY_$K_SET_INPUT_BREAK_MODE(short *line_ptr, void *mode_ptr,
                                        status_$t *status);

// TTY_$K_INQ_INPUT_BREAK_MODE - Inquire input break mode
// @param line_ptr: Pointer to terminal line number
// @param mode_ptr: Pointer to receive break mode structure
// @param status: Pointer to receive status code
extern void TTY_$K_INQ_INPUT_BREAK_MODE(short *line_ptr, void *mode_ptr,
                                        status_$t *status);

// TTY_$K_SET_PGROUP - Set process group UID
// @param line_ptr: Pointer to terminal line number
// @param uid_ptr: Pointer to UID structure
// @param status: Pointer to receive status code
extern void TTY_$K_SET_PGROUP(short *line_ptr, uid_t *uid_ptr,
                              status_$t *status);

// TTY_$K_INQ_PGROUP - Inquire process group UID
// @param line_ptr: Pointer to terminal line number
// @param uid_ptr: Pointer to receive UID structure
// @param status: Pointer to receive status code
extern void TTY_$K_INQ_PGROUP(short *line_ptr, uid_t *uid_ptr,
                              status_$t *status);

// TTY_$K_SET_SESSION_ID - Set session ID
// @param line_ptr: Pointer to terminal line number
// @param sid_ptr: Pointer to session ID
// @param status: Pointer to receive status code
extern void TTY_$K_SET_SESSION_ID(short *line_ptr, short *sid_ptr,
                                  status_$t *status);

// TTY_$K_INQ_SESSION_ID - Inquire session ID
// @param line_ptr: Pointer to terminal line number
// @param sid_ptr: Pointer to receive session ID
// @param status: Pointer to receive status code
extern void TTY_$K_INQ_SESSION_ID(short *line_ptr, short *sid_ptr,
                                  status_$t *status);

// TTY_$K_SET_DELAY - Set delay value
// @param line_ptr: Pointer to terminal line number
// @param delay_type_ptr: Pointer to delay type (0-4)
// @param value_ptr: Pointer to delay value
// @param status: Pointer to receive status code
extern void TTY_$K_SET_DELAY(short *line_ptr, ushort *delay_type_ptr,
                             short *value_ptr, status_$t *status);

// TTY_$K_INQ_DELAY - Inquire delay value
// @param line_ptr: Pointer to terminal line number
// @param delay_type_ptr: Pointer to delay type (0-4)
// @param value_ptr: Pointer to receive delay value
// @param status: Pointer to receive status code
extern void TTY_$K_INQ_DELAY(short *line_ptr, ushort *delay_type_ptr,
                             short *value_ptr, status_$t *status);

// TTY_$K_DRAIN_OUTPUT - Wait for output buffer to drain
// @param line_ptr: Pointer to terminal line number
// @param status: Pointer to receive status code
extern void TTY_$K_DRAIN_OUTPUT(short *line_ptr, status_$t *status);

#endif /* TTY_H */
