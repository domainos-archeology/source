// TTY (Teletype) subsystem header for Domain/OS
// Provides terminal line discipline handling

#ifndef TTY_H
#define TTY_H

#include "../base/base.h"

// =============================================================================
// TTY Constants
// =============================================================================

// Input/output buffer size (circular buffer with 256 entries, indices 1-256)
#define TTY_BUFFER_SIZE         0x100

// Maximum number of function characters
#define TTY_MAX_FUNC_CHARS      0x12

// Character classes for function character mapping
#define TTY_CHAR_CLASS_SIGINT   0x00    // Interrupt (^C)
#define TTY_CHAR_CLASS_SIGQUIT  0x01    // Quit (^\)
#define TTY_CHAR_CLASS_SIGTSTP  0x02    // Suspend (^Z)
#define TTY_CHAR_CLASS_BREAK    0x03    // Break character (end of line)
#define TTY_CHAR_CLASS_EOF      0x04    // End of file (^D)
#define TTY_CHAR_CLASS_XON      0x05    // Resume output (^Q)
#define TTY_CHAR_CLASS_XOFF     0x06    // Stop output (^S)
#define TTY_CHAR_CLASS_DEL      0x07    // Delete character
#define TTY_CHAR_CLASS_WERASE   0x08    // Word erase
#define TTY_CHAR_CLASS_KILL     0x09    // Kill line
#define TTY_CHAR_CLASS_REPRINT  0x0A    // Reprint line
#define TTY_CHAR_CLASS_NL       0x0B    // Newline
#define TTY_CHAR_CLASS_DISCARD  0x0C    // Discard output (^O)
#define TTY_CHAR_CLASS_FLUSHOUT 0x0D    // Flush output
#define TTY_CHAR_CLASS_CR       0x0E    // Carriage return
#define TTY_CHAR_CLASS_CRLF     0x0F    // CR/LF handling
#define TTY_CHAR_CLASS_TAB      0x10    // Tab character
#define TTY_CHAR_CLASS_CRASH    0x11    // Crash system (debug)
#define TTY_CHAR_CLASS_NORMAL   0x12    // Normal character (no special handling)

// Signal numbers used with TTY_$I_SIGNAL
#define TTY_SIG_HUP             0x01    // Hangup
#define TTY_SIG_INT             0x02    // Interrupt
#define TTY_SIG_QUIT            0x03    // Quit
#define TTY_SIG_TSTP            0x15    // Terminal stop (^Z)
#define TTY_SIG_WINCH           0x1A    // Window size change
#define TTY_SIG_CONT            0x16    // Continue

// =============================================================================
// TTY State Flags (offset 0x08-0x09 in tty_desc_t)
// =============================================================================
#define TTY_FLAG_PARITY_ERR     0x0080  // Parity error on current char
#define TTY_FLAG_RAW_MODE       0x0040  // Raw mode (no char class processing)

// =============================================================================
// TTY Status Flags (offset 0x09 in tty_desc_t)
// =============================================================================
#define TTY_STATUS_OUTPUT_WAIT  0x01    // Waiting for output buffer drain
#define TTY_STATUS_INPUT_WAIT   0x02    // Waiting for input
#define TTY_STATUS_XON_XOFF     0x04    // XON/XOFF active
#define TTY_STATUS_SIG_PEND     0x10    // Signal pending on input
#define TTY_STATUS_OUTPUT_FLUSH 0x20    // Output flush in progress
#define TTY_STATUS_EOF_PEND     0x40    // EOF pending

// =============================================================================
// TTY Error Flags (offset 0x0B in tty_desc_t)
// =============================================================================
#define TTY_ERR_CALLBACK        0x01    // Error callback set
#define TTY_ERR_OVERFLOW        0x02    // Input buffer overflow

// =============================================================================
// TTY Callback Descriptor
// Each TTY has up to 6 signal callback entries (12 bytes each)
// =============================================================================
typedef struct tty_signal_entry {
    m68k_ptr_t  tty_desc;           // 0x00: Back pointer to TTY descriptor
    m68k_ptr_t  callback;           // 0x04: Callback function pointer
    uint16_t    signal_num;         // 0x08: Signal number
    uint16_t    reserved;           // 0x0A: Reserved/padding
} tty_signal_entry_t;

_Static_assert(sizeof(tty_signal_entry_t) == 0x0C, "tty_signal_entry_t must be 12 bytes");

// =============================================================================
// TTY Descriptor Structure
// Main control structure for a TTY line (approx 0x4DC bytes)
// =============================================================================
typedef struct tty_desc {
    // Basic identification and state (0x00-0x0F)
    uint32_t    line_id;            // 0x00: Terminal line identifier
    m68k_ptr_t  handler_ptr;        // 0x04: Handler structure pointer
    uint16_t    state_flags;        // 0x08: State flags (raw mode, parity, etc.)
    uint16_t    pending_signal;     // 0x0A: Pending signal number
    uint8_t     output_flags;       // 0x0C: Output control flags (TTY_FLAG_*)
    uint8_t     status_flags;       // 0x0D: Status flags (wait states)
    uint16_t    reserved_0E;        // 0x0E: Reserved

    // Mode flags (0x10-0x1F)
    uint32_t    reserved_10;        // 0x10: Reserved
    uint32_t    input_flags;        // 0x14: Input processing flags
    uint16_t    reserved_18;        // 0x18: Reserved
    uint16_t    reserved_1A;        // 0x1A: Reserved
    uint32_t    echo_flags;         // 0x1C: Echo control flags

    // Function character control (0x20-0x3F)
    uint32_t    func_enabled;       // 0x20: Bitmask of enabled function chars
    uint8_t     func_chars[TTY_MAX_FUNC_CHARS];  // 0x24: Function character bindings
    uint16_t    reserved_36;        // 0x36: Reserved/padding

    // Input break mode (0x38-0x3F)
    uint16_t    break_mode;         // 0x38: Input break mode (0=raw, 1-3=line modes)
    uint16_t    min_chars;          // 0x3A: Minimum chars before signaling reader
    uint32_t    reserved_3C;        // 0x3C: Reserved

    // Delay settings (0x40-0x4F)
    uint16_t    delay[5];           // 0x40: Delay settings for various operations
    uint16_t    reserved_4A;        // 0x4A: Reserved

    // Process group ownership (0x4C-0x5B)
    uid_$t      pgroup_uid;         // 0x4C: Process group UID (8 bytes)
    uint16_t    session_id;         // 0x54: Session ID
    uint16_t    saved_input_flags;  // 0x56: Saved input flags state
    uint16_t    current_input_flags; // 0x58: Current input flags
    uint16_t    reserved_5A;        // 0x5A: Reserved

    // Signal callback entries (0x5C-0xA3) - 6 entries of 12 bytes each
    tty_signal_entry_t signals[6];  // 0x5C: Signal callback entries

    // Character class table (0xA4-0x2A3) - 256 entries of 2 bytes each
    uint16_t    char_class[256];    // 0xA4: Character class for each byte value

    // Handler function pointers (0x2A4-0x2C3)
    m68k_ptr_t  input_ec;           // 0x2A4: Input eventcount pointer
    m68k_ptr_t  output_ec;          // 0x2A8: Output eventcount pointer
    m68k_ptr_t  reserved_2AC;       // 0x2AC: Reserved
    m68k_ptr_t  err_handler;        // 0x2B0: Error handler function
    m68k_ptr_t  reserved_2B4;       // 0x2B4: Reserved
    m68k_ptr_t  xon_xoff_handler;   // 0x2B8: XON/XOFF handler
    m68k_ptr_t  flow_ctrl_handler;  // 0x2BC: Flow control handler
    m68k_ptr_t  status_handler;     // 0x2C0: Status change handler
    m68k_ptr_t  reserved_2C4;       // 0x2C4: Reserved

    // Input buffer control (0x2C8-0x2D0)
    uint16_t    reserved_2C8;       // 0x2C8: Reserved
    uint16_t    input_head;         // 0x2CA: Input buffer head index (1-256)
    uint16_t    input_read;         // 0x2CC: Input buffer read position
    uint16_t    input_tail;         // 0x2CE: Input buffer tail index (1-256)

    // Input buffer (0x2D1-0x3D0) - 256 bytes starting at index 1
    uint8_t     input_buffer[TTY_BUFFER_SIZE]; // 0x2D0: Circular input buffer

    // Output buffer control (0x3D0-0x3D8)
    uint16_t    reserved_3D0;       // 0x3D0: Reserved
    uint16_t    output_head;        // 0x3D2: Output buffer head index
    uint16_t    output_read;        // 0x3D4: Output buffer read position
    uint16_t    output_tail;        // 0x3D6: Output buffer tail index

    // Output buffer (0x3D8-0x4D7) - 256 bytes
    uint8_t     output_buffer[TTY_BUFFER_SIZE]; // 0x3D8: Circular output buffer

    // Crash/debug settings (0x4D8-0x4DB)
    uint8_t     crash_char;         // 0x4D8: Crash character (if enabled)
    uint8_t     raw_mode;           // 0x4D9: Raw mode flag (nonzero = raw)
    uint16_t    reserved_4DA;       // 0x4DA: Reserved

} tty_desc_t;

// =============================================================================
// Global TTY data
// =============================================================================
extern uint32_t TTY_$SPIN_LOCK;     // Spin lock for TTY operations (at 0xe2dd74)

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
// @param options: Read options
// @param buffer: Buffer to receive characters
// @param count: Pointer to max count (updated with actual count)
// @param status: Pointer to receive status code
// @return: Number of characters read
extern ushort TTY_$K_GET(short *line_ptr, int options, void *buffer,
                         ushort *count, status_$t *status);

// TTY_$K_PUT - Write characters to TTY
// @param line_ptr: Pointer to terminal line number
// @param options: Write options
// @param buffer: Buffer containing characters
// @param count: Pointer to count of characters (updated on return)
// @param status: Pointer to receive status code
extern void TTY_$K_PUT(short *line_ptr, int options, void *buffer,
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
extern void TTY_$K_INQ_FLAGS(short *line_ptr, uint16_t *flags_ptr, status_$t *status);

// TTY_$K_SET_FUNC_CHAR - Set function character binding
// @param line_ptr: Pointer to terminal line number
// @param func_ptr: Pointer to function number
// @param ch_ptr: Pointer to character value
// @param status: Pointer to receive status code
extern void TTY_$K_SET_FUNC_CHAR(short *line_ptr, ushort *func_ptr, char *ch_ptr,
                                 status_$t *status);

// TTY_$K_INQ_FUNC_CHAR - Inquire function character binding
// @param line_ptr: Pointer to terminal line number
// @param func_ptr: Pointer to function number
// @param ch_ptr: Pointer to receive character value
// @param status: Pointer to receive status code
extern void TTY_$K_INQ_FUNC_CHAR(short *line_ptr, ushort *func_ptr, char *ch_ptr,
                                 status_$t *status);

// TTY_$K_ENABLE_FUNC - Enable/disable function character
// @param line_ptr: Pointer to terminal line number
// @param func_ptr: Pointer to function number
// @param enable_ptr: Pointer to enable flag (nonzero = enable)
// @param status: Pointer to receive status code
extern void TTY_$K_ENABLE_FUNC(short *line_ptr, ushort *func_ptr, char *enable_ptr,
                               status_$t *status);

// TTY_$K_INQ_FUNC_ENABLED - Inquire enabled function characters
// @param line_ptr: Pointer to terminal line number
// @param enabled_ptr: Pointer to receive enabled bitmask
// @param status: Pointer to receive status code
extern void TTY_$K_INQ_FUNC_ENABLED(short *line_ptr, uint32_t *enabled_ptr,
                                    status_$t *status);

// TTY_$K_SET_INPUT_FLAG - Set input processing flag
// @param line_ptr: Pointer to terminal line number
// @param flag_ptr: Pointer to flag number
// @param value_ptr: Pointer to value (nonzero = set)
// @param status: Pointer to receive status code
extern void TTY_$K_SET_INPUT_FLAG(short *line_ptr, ushort *flag_ptr, char *value_ptr,
                                  status_$t *status);

// TTY_$K_INQ_INPUT_FLAGS - Inquire input processing flags
// @param line_ptr: Pointer to terminal line number
// @param flags_ptr: Pointer to receive flags
// @param status: Pointer to receive status code
extern void TTY_$K_INQ_INPUT_FLAGS(short *line_ptr, uint32_t *flags_ptr,
                                   status_$t *status);

// TTY_$K_SET_OUTPUT_FLAG - Set output processing flag
// @param line_ptr: Pointer to terminal line number
// @param flag_ptr: Pointer to flag number
// @param value_ptr: Pointer to value (nonzero = set)
// @param status: Pointer to receive status code
extern void TTY_$K_SET_OUTPUT_FLAG(short *line_ptr, ushort *flag_ptr, char *value_ptr,
                                   status_$t *status);

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
extern void TTY_$K_SET_ECHO_FLAG(short *line_ptr, ushort *flag_ptr, char *value_ptr,
                                 status_$t *status);

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
extern void TTY_$K_SET_PGROUP(short *line_ptr, uid_$t *uid_ptr, status_$t *status);

// TTY_$K_INQ_PGROUP - Inquire process group UID
// @param line_ptr: Pointer to terminal line number
// @param uid_ptr: Pointer to receive UID structure
// @param status: Pointer to receive status code
extern void TTY_$K_INQ_PGROUP(short *line_ptr, uid_$t *uid_ptr, status_$t *status);

// TTY_$K_SET_SESSION_ID - Set session ID
// @param line_ptr: Pointer to terminal line number
// @param sid_ptr: Pointer to session ID
// @param status: Pointer to receive status code
extern void TTY_$K_SET_SESSION_ID(short *line_ptr, short *sid_ptr, status_$t *status);

// TTY_$K_INQ_SESSION_ID - Inquire session ID
// @param line_ptr: Pointer to terminal line number
// @param sid_ptr: Pointer to receive session ID
// @param status: Pointer to receive status code
extern void TTY_$K_INQ_SESSION_ID(short *line_ptr, short *sid_ptr, status_$t *status);

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
