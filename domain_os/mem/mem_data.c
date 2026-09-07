/*
 * mem_data.c - MEM module A5 block
 *
 * The SAU2 map has one data segment for this module:
 *
 *     D  E22930  MEM_   size = 5C
 *        E22930  MEM_$SIZE
 *        E22934  MEM_$MEM_REC
 *
 * and MEM_$PARITY_LOG establishes it as a single base register
 * (`lea (0xe22930).l,A5` at 0x00E0ADB8), so it is defined here as one
 * object.  Its layout is mem_data_t in mem/mem.h; the offsets that layout
 * asserts are the ones the accessing instructions prove:
 *
 *   0xE22930  MEM_$SIZE                     longword, module offset 0x00
 *   0xE22934  MEM_$MEM_REC.w_00             word,  image value 0x0002
 *   0xE22936  MEM_$MEM_REC.w_02             word,  image value 0x0002
 *   0xE22938  MEM_$BOARD_ERRORS[0]          word,  never touched (bias slot)
 *   0xE2293A  MEM_$BOARD_ERRORS[1]          word,  00e0add4 (0x8,A5,D1) D1=2
 *   0xE2293C  MEM_$BOARD_ERRORS[2]          word,  00e0add4 (0x8,A5,D1) D1=4
 *   0xE2293E  MEM_$MEM_REC.w_0a             word,  no references
 *   0xE22940  MEM_$MEM_REC.w_0c             word,  no references
 *   0xE22942  MEM_$PAGE_ERRORS[0..3]        4 x 18 bytes, 00e0adde
 *   0xE2298A  tail word                     outside MEM_$MEM_REC
 *
 * Initial contents are `gsk read 0x00E22930 0x5C`: all zero except the two
 * words at 0xE22934 and 0xE22936.
 */

#include "mem/mem_internal.h"

mem_data_t MEM_DATA = {
    .rec = {
        .w_00 = 0x0002,
        .w_02 = 0x0002,
    },
};
