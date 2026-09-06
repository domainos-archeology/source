/*
 * sio6509/sio6509.h - SIO6509 Console Serial Controller Public Interface
 *
 * The 6509 is the keyboard/display console serial controller.  No C
 * implementations exist yet; this header carries the prototype needed by
 * TERM_$INIT (term/init.c).
 *
 * Original addresses:
 *   SIO6509_$RCV       0x00e1d53e
 *   SIO6509_$XMIT      0x00e1d586
 *   SIO6509_$INIT      0x00e3350c
 *   SIO6509_$SET_LINE  0x00e72656
 *   SIO6509_$INQ_LINE  0x00e72668
 */

#ifndef SIO6509_H
#define SIO6509_H

#include "base/base.h"

/*
 * SIO6509_$INIT - Initialize a 6509 console serial channel
 *
 * From the assembly at 0x00e3350c (link -0x4, 5 longword arguments):
 *   int_vec_ptr  - (0x08,A6) pointer to interrupt vector number; the channel
 *                  struct address is stored in the 6509 unit table at
 *                  0xe2dfd4 + vec*4 and the vector slot at 0x64 + vec*4 is
 *                  pointed at 0xe2dfe8 (the interrupt entry).
 *   chip_num_ptr - (0x0c,A6) pointer to chip number; hardware base is
 *                  0xffb000 - 0x20 + chip*0x20.
 *   chan_struct  - (0x10,A6) channel structure: [0] = hardware base,
 *                  [4] = *callback.
 *   callback     - (0x14,A6) pointer to the SIO descriptor address
 *                  (dereferenced once).
 *   config       - (0x18,A6) two configuration bytes written to hardware
 *                  register +1.
 */
void SIO6509_$INIT(int16_t *int_vec_ptr, int16_t *chip_num_ptr,
                   void *chan_struct, m68k_ptr_t *callback, uint8_t *config);

#endif /* SIO6509_H */
