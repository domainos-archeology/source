/*
 * kbd/kbd_internal.h - Internal Keyboard Controller Definitions
 *
 * Contains internal functions, data structures, and types used only within
 * the keyboard subsystem. External consumers should use kbd/kbd.h.
 */

#ifndef KBD_INTERNAL_H
#define KBD_INTERNAL_H

#include "kbd/kbd.h"
#include "ec/ec.h"
#include "term/term_internal.h"
#include "mmu/mmu.h"
#include "time/time.h"
#include "misc/crash_system.h"
#include "dxm/dxm.h"
#include "suma/suma.h"

/*
 * ============================================================================
 * Keyboard State Structure
 * ============================================================================
 *
 * This structure holds the state for a keyboard/terminal line.
 * Size: approximately 0xA4 bytes
 */
typedef struct kbd_state_t {
    void *handler;              /* 0x00: Handler function pointer (0 = normal mode) */
    uint8_t pad_04[0x10];       /* 0x04: Unknown */
    uint32_t user_data;         /* 0x14: User data for handler */
    uint32_t last_time;         /* 0x18: Last event time */
    uint32_t delta_time;        /* 0x1C: Delta time for tpad */
    uint32_t clock_high;        /* 0x20: Clock high word */
    uint16_t clock_low;         /* 0x24: Clock low word */
    uint8_t tpad_x;             /* 0x26: Touchpad X coordinate */
    uint8_t tpad_y;             /* 0x27: Touchpad Y byte 1 */
    uint8_t tpad_z;             /* 0x28: Touchpad Y byte 2 */
    uint8_t pad_29[0x03];       /* 0x29: Padding */
    uint8_t *tpad_ptr;          /* 0x2C: Current touchpad buffer pointer */
    void *tpad_buffer;          /* 0x30: Pointer to TERM_$TPAD_BUFFER */
    void *ktt_ptr;              /* 0x34: Keyboard translation table pointer */
    uint16_t state;             /* 0x38: Current state machine state */
    uint16_t sub_state;         /* 0x3A: Sub-state for key processing */
    uint16_t kbd_type_idx;      /* 0x3C: Keyboard type index */
    uint16_t pending_mode;      /* 0x3E: Pending keyboard mode */
    uint8_t kbd_type_str[4];    /* 0x40: Keyboard type string */
    uint16_t kbd_type_len;      /* 0x44: Keyboard type string length */
    uint16_t flags;             /* 0x46: Flags */
    uint8_t pad_48[0x04];       /* 0x48: Padding */
    ec_$eventcount_t ec;        /* 0x4C: Event counter (12 bytes) */
    uint32_t ring_head;         /* 0x58: Ring buffer head index */
    uint16_t ring_tail;         /* 0x5A: Ring buffer tail index */
    uint8_t pad_5C;             /* 0x5C: Padding */
    uint8_t ring_buffer[0x40];  /* 0x5D: Key ring buffer (64 bytes) */
    uint8_t pad_9D;             /* 0x9D: Padding */
    uint32_t flags2;            /* 0x9E: Secondary flags (0x10001) */
    uint16_t value2;            /* 0xA2: Secondary value (0x40) */
} kbd_state_t;

/*
 * ============================================================================
 * Global Data Declarations
 * ============================================================================
 */

/*
 * KBD_$MODE_TABLE - Keyboard mode translation table
 * Maps internal mode values to external mode codes.
 * Located at 0xe2dde4
 */
extern uint8_t KBD_$MODE_TABLE[];

/*
 * TERM_$MAX_DTTE is provided as a macro in term/term.h (included via term/term_internal.h)
 * aliasing TERM_$DATA.max_dtte at offset 0x1388.
 */

/*
 * DAT_00e2dcbc - Base of DTTE table (offset 0x2C within each 0x38-byte entry)
 */
extern uint8_t DAT_00e2dcbc[];

/*
 * DAT_00e2ddec - State transition table
 */
extern uint16_t DAT_00e2ddec[];

/*
 * MNK_$KTT_PTRS - Keyboard translation table pointers
 * Located at 0xe273dc
 */
extern void *MNK_$KTT_PTRS[];

/*
 * MNK_$KTT_MAX - Maximum keyboard translation table index
 * Located at 0xe273fc
 */
extern int16_t MNK_$KTT_MAX;

/*
 * SMD_$KTT - SMD keyboard translation table
 * Located at 0xe273fe
 */
extern uint8_t SMD_$KTT[];


/*
 * ============================================================================
 * Internal Function Declarations
 * ============================================================================
 */

/*
 * FUN_00e1c9fc - Keyboard state machine lookup
 * Returns pointer to state entry in A0
 */
void *kbd_$state_lookup(uint16_t state, uint8_t key);

/*
 * FUN_00e1ca8c - Set keyboard type
 * Copies type string and looks up translation table
 */
void kbd_$set_type(kbd_state_t *state, uint8_t *type_str, uint16_t type_len);

/*
 * FUN_00e1cafe - Fetch key from ring buffer
 * Returns -1 if key available, 0 if buffer empty
 */
int8_t kbd_$fetch_key(kbd_state_t *state, uint8_t *key_out, int16_t *mode_out);

/*
 * FUN_00e1cc10 - Process normal key
 */
void kbd_$process_key(uint8_t key, kbd_state_t *state);

/*
 * FUN_00e1cc64 - Translate key code
 */
uint8_t kbd_$translate_key(uint8_t key);

/*
 * FUN_00e1ca62 - Get keyboard mode from key
 */
int16_t kbd_$get_mode(uint8_t key);

#endif /* KBD_INTERNAL_H */
