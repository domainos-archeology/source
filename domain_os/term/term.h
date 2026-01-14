#ifndef TERM_H
#define TERM_H

#include "base/base.h"
#include "ml/ml.h"

// Maximum number of terminal lines
#define TERM_MAX_LINES 4

// =============================================================================
// DTTE - Display Terminal Table Entry
// Each terminal line has a 0x38 (56) byte entry containing I/O state
// Located at offset 0x12a0 from TERM_$DATA base, indexed by line number
// =============================================================================
typedef struct dtte {
  char reserved_00[0x0c]; // 0x00: unknown
  m68k_ptr_t input_ec;    // 0x0c: input eventcount pointer
  char reserved_10[0x08]; // 0x10: unknown
  m68k_ptr_t output_ec;   // 0x18: output eventcount pointer
  char reserved_1c[0x08]; // 0x1c: unknown
  m68k_ptr_t
      handler_ptr; // 0x24: handler pointer (copied to TTY struct offset 4)
  m68k_ptr_t tty_handler; // 0x28: TTY handler structure pointer
  m68k_ptr_t alt_handler; // 0x2c: alternate handler pointer
  m68k_ptr_t ptr_30;      // 0x30: another pointer (purpose TBD)
  int16_t discipline;     // 0x34: terminal discipline (0=TTY, 1=disable alt,
                          // 2=enable alt, 3=SUMA)
  uint8_t flags; // 0x36: terminal flags (bit 7 = conditional read mode)
  char pad_37;   // 0x37: padding to 0x38 boundary
} dtte_t;

// Verify structure size
_Static_assert(sizeof(dtte_t) == 0x38, "dtte_t must be 56 bytes");

// =============================================================================
// Large terminal entry structure (0x4dc = 1244 bytes per line)
// Contains UID at offset 0x1a4 within each entry
// Used by TERM_$P2_CLEANUP for process cleanup
// =============================================================================
typedef struct term_line_data {
  char reserved_000[0x1a4]; // 0x000: unknown
  uid_$t owner_uid;         // 0x1a4: UID of owning process
  char reserved_1ac[0x4dc - 0x1a4 - sizeof(uid_$t)]; // 0x1ac to end
} term_line_data_t;

// Verify structure size
_Static_assert(sizeof(term_line_data_t) == 0x4dc,
               "term_line_data_t must be 1244 bytes");

// =============================================================================
// TERM_$DATA - Main terminal subsystem data structure
// Base address: 0xe2c9f0 in original binary
// =============================================================================
typedef struct term_data {
  // Global handler function pointers (offsets 0x00-0x27)
  char reserved_00[0x18];     // 0x00: unknown
  m68k_ptr_t ptr_tty_i_rcv;   // 0x18: PTR_TTY_$I_RCV
  m68k_ptr_t ptr_tty_i_drain; // 0x1c: PTR_TTY_$I_OUTPUT_BUFFER_DRAINED
  m68k_ptr_t ptr_tty_i_hup;   // 0x20: PTR_TTY_$I_HUP
  m68k_ptr_t ptr_tty_i_int;   // 0x24: PTR_TTY_$I_INTERRUPT

  char reserved_28[0x98]; // 0x28-0xbf: unknown

  m68k_ptr_t ptr_tty_i_rcv_alt; // 0xc0: PTR_TTY_$I_RCV_ALT

  char reserved_c4[0x94]; // 0xc4-0x157: unknown

  // Per-line data with 0x4dc stride (3 lines)
  // Offset 0x158: line_data[0] would start at 0xe2cb48
  // But the actual indexing is complex - offset 0x4dc from base,
  // with UID at -0x338 from iteration pointer
  char reserved_158[0x113c]; // 0x158-0x1293: complex per-line data

  uint16_t pchist_enable; // 0x1294: process history enable flag

  char reserved_1296[0x0a]; // 0x1296-0x129f: unknown

  // DTTE array (4 entries of 0x38 bytes each)
  dtte_t dtte[TERM_MAX_LINES]; // 0x12a0: Display Terminal Table Entries

  // After DTTE array: 0x12a0 + 4*0x38 = 0x1380
  char reserved_1380[0x04]; // 0x1380-0x1383: unknown

  m68k_ptr_t tty_spin_lock; // 0x1384: TTY_$SPIN_LOCK
  int16_t max_dtte;         // 0x1388: TERM_$MAX_DTTE (typically 3)

  char reserved_138a[0x06]; // 0x138a-0x138f: unknown

  char kbd_string_data[16]; // 0x1390: keyboard string data buffer (size TBD)
} term_data_t;

// Global TERM data structure (at 0xe2c9f0 in original binary)
extern term_data_t TERM_$DATA;

// =============================================================================
// Function declarations
// =============================================================================
extern void TERM_$STATUS_CONVERT(status_$t *status);
extern short TERM_$GET_REAL_LINE(short line_num, status_$t *status_ret);
extern void TERM_$SEND_KBD_STRING(void *str, void *length);
extern void TERM_$SET_DISCIPLINE(short *line_ptr, void *discipline,
                                 status_$t *status_ret);
extern void TERM_$SET_REAL_LINE_DISCIPLINE(unsigned short *line_ptr,
                                           short *discipline_ptr,
                                           status_$t *status_ret);
extern void TERM_$INQ_DISCIPLINE(short *line_ptr,
                                 unsigned short *discipline_ret,
                                 status_$t *status_ret);
extern void TERM_$INIT(short *param1, short *param2);
extern unsigned short TERM_$READ(short *line_ptr, unsigned int buffer,
                                 void *param3, status_$t *status_ret);
extern unsigned short TERM_$READ_COND(void *line_ptr, void *buffer,
                                      void *param3, status_$t *status_ret);
extern void TERM_$WRITE(void *line_ptr, void *buffer, unsigned short *count_ptr,
                        status_$t *status_ret);
extern void TERM_$CONTROL(short *line_ptr, unsigned short *option_ptr,
                          unsigned short *value_ptr, status_$t *status_ret);
extern void TERM_$INQUIRE(short *line_ptr, unsigned short *option_ptr,
                          unsigned short *value_ret, status_$t *status_ret);
extern void TERM_$GET_EC(unsigned short *ec_id, short *term_line,
                         ec2_$eventcount_t *ec_ret, status_$t *status_ret);
extern void TERM_$HELP_CALLBACK(void);
extern void TERM_$PCHIST_ENABLE(unsigned short *enable_ptr,
                                status_$t *status_ret);
extern void TERM_$ENQUEUE_TPAD(void **param1);
extern void TERM_$P2_CLEANUP(short *param1);

#endif /* TERM_H */
