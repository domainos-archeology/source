// TTY_$I_INIT - Initialize a TTY descriptor structure
// Address: 0x00e3324c
// Size: 278 bytes

#include "tty.h"

// Default function character bindings (at 0xe351d8 in original binary)
// These define which characters map to which function classes
static const uint8_t default_func_chars[TTY_MAX_FUNC_CHARS] = {
    0x03,   // 0x00: ^C -> SIGINT
    0x1C,   // 0x01: ^\ -> SIGQUIT
    0x1A,   // 0x02: ^Z -> SIGTSTP
    0x0A,   // 0x03: LF -> break
    0x04,   // 0x04: ^D -> EOF
    0x11,   // 0x05: ^Q -> XON
    0x13,   // 0x06: ^S -> XOFF
    0x7F,   // 0x07: DEL -> delete char
    0x17,   // 0x08: ^W -> word erase
    0x15,   // 0x09: ^U -> kill line
    0x12,   // 0x0A: ^R -> reprint
    0x0A,   // 0x0B: LF -> newline
    0x0F,   // 0x0C: ^O -> discard
    0x0D,   // 0x0D: CR -> flush output
    0x0D,   // 0x0E: CR -> carriage return
    0x0D,   // 0x0F: CR -> CR/LF handling
    0x09,   // 0x10: TAB -> tab
    0x00,   // 0x11: NUL -> crash (disabled by default)
};

// Signal callback initialization data (at 0xe351b0)
// Contains function pointers and configuration for the 6 signal entries
static const struct {
    m68k_ptr_t callback;
    uint16_t signal_num;
} default_signal_entries[6] = {
    { 0, 0 },   // Entry 0
    { 0, 0 },   // Entry 1
    { 0, 0 },   // Entry 2
    { 0, 0 },   // Entry 3
    { 0, 0 },   // Entry 4
    { 0, 0 },   // Entry 5
};

void TTY_$I_INIT(tty_desc_t *tty)
{
    short i;

    // Clear state flags
    tty->state_flags = 0;

    // Set default mode flags
    // input_flags at 0x14 = 0x29
    tty->input_flags = 0x29;

    // output_flags at 0x0C = 2
    tty->output_flags = 2;

    // echo_flags at 0x1C = 0x23
    tty->echo_flags = 0x23;

    // Clear reserved fields at 0x18, 0x10
    tty->reserved_18 = 0;
    tty->reserved_10 = 0;

    // Clear crash character
    tty->crash_char = 0;

    // Copy default function character bindings
    for (i = 0; i < TTY_MAX_FUNC_CHARS; i++) {
        tty->func_chars[i] = default_func_chars[i];
    }

    // Set enabled function mask (0x1ffef = all except crash)
    tty->func_enabled = 0x1FFEF;

    // Initialize character class table - all chars get class 0x12 (normal)
    for (i = 0; i < 256; i++) {
        tty->char_class[i] = TTY_CHAR_CLASS_NORMAL;
    }

    // Clear break mode
    tty->break_mode = 0;

    // Clear process group UID to nil
    tty->pgroup_uid.high = UID_$NIL.high;
    tty->pgroup_uid.low = UID_$NIL.low;

    // Clear session and input flags
    tty->session_id = 0;
    tty->current_input_flags = 0;
    tty->saved_input_flags = 0;
    tty->pending_signal = 0;

    // Apply function character mappings to character class table
    // FUN_00e6726e(param_1, 0xff)
    // This function updates char_class[] based on func_chars[] and func_enabled

    // Clear raw mode flag
    tty->raw_mode = 0;

    // Initialize signal callback entries
    for (i = 0; i < 6; i++) {
        tty->signals[i].tty_desc = (m68k_ptr_t)(uintptr_t)tty;
        tty->signals[i].callback = default_signal_entries[i].callback;
        tty->signals[i].signal_num = default_signal_entries[i].signal_num;
    }

    // Initialize input buffer (circular buffer indices start at 1)
    tty->input_head = 1;
    tty->input_read = 1;
    tty->input_tail = 1;
    tty->reserved_2C8 = 0x100;

    // Initialize output buffer
    tty->output_head = 1;
    tty->output_read = 1;
    tty->output_tail = 0x100;
}
