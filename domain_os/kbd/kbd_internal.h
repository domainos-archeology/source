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
#include "term/term.h"          /* TERM_$MAX_DTTE */
#include "mmu/mmu.h"
#include "time/time.h"
#include "misc/crash_system.h"
#include "dxm/dxm.h"
#include "smd/smd.h"   /* SMD_$KTT */
#include "suma/suma.h"

/*
 * Key ring capacity.  kbd_$fetch_key and kbd_$process_key both wrap the
 * 1-based index at 0x40 (00e1cb34 / 00e1cc20 `cmpi.w #0x40,...`), and
 * KBD_$INIT stores 0x40 into the size word at +0x5C (00e333be).
 */
#define KBD_RING_SIZE 0x40

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
    /* Key ring.  Both indices are Pascal 1-based and run 1..KBD_RING_SIZE:
     * kbd_$fetch_key reads the head with `move.w (0x58,A4),D2w` (00e1cb24)
     * and kbd_$process_key the tail with `cmpi.w #0x40,(0x5a,A2)` /
     * `move.w (0x5a,A2),D1w` (00e1cc20, 00e1cc30).  KBD_$INIT seeds both at
     * once with `move.l #0x10001,(0x58,A2)` (00e333b6), so head and tail are
     * two words at 0x58/0x5A -- not one longword. */
    uint16_t ring_head;         /* 0x58: Ring buffer head index (1-based) */
    uint16_t ring_tail;         /* 0x5A: Ring buffer tail index (1-based) */
    uint16_t ring_size;         /* 0x5C: Ring capacity, 0x40 (00e333be) */
    /* Element i lives at state+0x5D+i (00e1cb30 `move.b (0x5d,A4,D2w*0x1)`,
     * 00e1cc48), i.e. ring_buffer[i-1] -- the array starts at 0x5E. */
    uint8_t ring_buffer[KBD_RING_SIZE]; /* 0x5E: Key ring buffer (64 bytes) */
    uint32_t flags2;            /* 0x9E: Secondary flags (0x10001) */
    uint16_t value2;            /* 0xA2: Secondary value (0x40) */
} kbd_state_t;

/* Layout recovered from the disassembly -- see the field comments above. */
#if defined(ARCH_M68K)
_Static_assert(__builtin_offsetof(kbd_state_t, handler) == 0x00, "kbd_state_t.handler");
_Static_assert(__builtin_offsetof(kbd_state_t, pad_04) == 0x04, "kbd_state_t.pad_04");
_Static_assert(__builtin_offsetof(kbd_state_t, user_data) == 0x14, "kbd_state_t.user_data");
_Static_assert(__builtin_offsetof(kbd_state_t, last_time) == 0x18, "kbd_state_t.last_time");
_Static_assert(__builtin_offsetof(kbd_state_t, delta_time) == 0x1C, "kbd_state_t.delta_time");
_Static_assert(__builtin_offsetof(kbd_state_t, clock_high) == 0x20, "kbd_state_t.clock_high");
_Static_assert(__builtin_offsetof(kbd_state_t, clock_low) == 0x24, "kbd_state_t.clock_low");
_Static_assert(__builtin_offsetof(kbd_state_t, tpad_x) == 0x26, "kbd_state_t.tpad_x");
_Static_assert(__builtin_offsetof(kbd_state_t, tpad_y) == 0x27, "kbd_state_t.tpad_y");
_Static_assert(__builtin_offsetof(kbd_state_t, tpad_z) == 0x28, "kbd_state_t.tpad_z");
_Static_assert(__builtin_offsetof(kbd_state_t, pad_29) == 0x29, "kbd_state_t.pad_29");
_Static_assert(__builtin_offsetof(kbd_state_t, tpad_ptr) == 0x2C, "kbd_state_t.tpad_ptr");
_Static_assert(__builtin_offsetof(kbd_state_t, tpad_buffer) == 0x30, "kbd_state_t.tpad_buffer");
_Static_assert(__builtin_offsetof(kbd_state_t, ktt_ptr) == 0x34, "kbd_state_t.ktt_ptr");
_Static_assert(__builtin_offsetof(kbd_state_t, state) == 0x38, "kbd_state_t.state");
_Static_assert(__builtin_offsetof(kbd_state_t, sub_state) == 0x3A, "kbd_state_t.sub_state");
_Static_assert(__builtin_offsetof(kbd_state_t, kbd_type_idx) == 0x3C, "kbd_state_t.kbd_type_idx");
_Static_assert(__builtin_offsetof(kbd_state_t, pending_mode) == 0x3E, "kbd_state_t.pending_mode");
_Static_assert(__builtin_offsetof(kbd_state_t, kbd_type_str) == 0x40, "kbd_state_t.kbd_type_str");
_Static_assert(__builtin_offsetof(kbd_state_t, kbd_type_len) == 0x44, "kbd_state_t.kbd_type_len");
_Static_assert(__builtin_offsetof(kbd_state_t, flags) == 0x46, "kbd_state_t.flags");
_Static_assert(__builtin_offsetof(kbd_state_t, pad_48) == 0x48, "kbd_state_t.pad_48");
_Static_assert(__builtin_offsetof(kbd_state_t, ec) == 0x4C, "kbd_state_t.ec");
_Static_assert(__builtin_offsetof(kbd_state_t, ring_head) == 0x58, "kbd_state_t.ring_head");
_Static_assert(__builtin_offsetof(kbd_state_t, ring_tail) == 0x5A, "kbd_state_t.ring_tail");
_Static_assert(__builtin_offsetof(kbd_state_t, ring_size) == 0x5C, "kbd_state_t.ring_size");
_Static_assert(__builtin_offsetof(kbd_state_t, ring_buffer) == 0x5E, "kbd_state_t.ring_buffer");
_Static_assert(__builtin_offsetof(kbd_state_t, flags2) == 0x9E, "kbd_state_t.flags2");
_Static_assert(__builtin_offsetof(kbd_state_t, value2) == 0xA2, "kbd_state_t.value2");
_Static_assert(sizeof(kbd_state_t) == 0xA4, "kbd_state_t size");
#endif

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
 * TERM_$MAX_DTTE is provided as a macro in term/term.h (included above)
 * aliasing TERM_$DATA.max_dtte at offset 0x1388.
 */

/*
 * DAT_00e2dcbc - Base of DTTE table (offset 0x2C within each 0x38-byte entry)
 */
extern uint8_t DAT_00e2dcbc[];

/*
 * DAT_00e2ddec - State transition table
 */
extern uint16_t DAT_00e2ddec[8];

/*
 * DAT_00e2ddfc - 0x00E2DDFC, the 32 words between DAT_00e2ddec and
 * TERM_$TPAD_BUFFER in the map segment "D E2DDE4 KBD size = D4".  No
 * instruction in the image reaches them; see kbd/kbd_data.c.
 */
extern uint16_t DAT_00e2ddfc[32];

/* MNK_$KTT_PTRS (0x00E273DC), MNK_$KTT_MAX (0x00E273FC) and SMD_$KTT are
 * cells of the SMD_WIRED module (SAU2 map, 0xE26F20 size 0x5E0), so they are
 * declared in smd/smd.h (bead source-3uo).  Their storage is still defined by
 * kbd/kbd_data.c. */


/*
 * ============================================================================
 * Internal Function Declarations
 * ============================================================================
 */

/*
 * kbd_$state_lookup (0x00e1c9fc) - Keyboard state machine lookup
 * Returns pointer to state entry in A0
 */
void *kbd_$state_lookup(uint16_t state, uint8_t key);

/*
 * kbd_$set_type (0x00e1ca8c) - Set keyboard type
 * Copies type string and looks up translation table
 */
void kbd_$set_type(kbd_state_t *state, uint8_t *type_str, uint16_t type_len);

/*
 * kbd_$fetch_key (0x00e1cafe) - Fetch key from ring buffer
 * Returns -1 if key available, 0 if buffer empty
 */
int8_t kbd_$fetch_key(kbd_state_t *state, uint8_t *key_out, int16_t *mode_out);

/*
 * kbd_$process_key (0x00e1cc10) - Process normal key
 */
void kbd_$process_key(uint8_t key, kbd_state_t *state);

/*
 * kbd_$translate_key (0x00e1cc64) - Translate key code
 */
uint8_t kbd_$translate_key(uint8_t key);

/*
 * kbd_$get_mode (0x00e1ca62) - Get keyboard mode from key
 */
int16_t kbd_$get_mode(uint8_t key);

#endif /* KBD_INTERNAL_H */
