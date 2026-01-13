#include "math.h"

// Signed multiplication: long * long -> int
// Handles signs by converting to unsigned multiply, then adjusts result sign
// Returns lower 32 bits of product
long M$MIS$LLL(long multiplicand,long multiplier) {
  ulong uVar1;
  ushort local_2;
  
  local_2 = 0;
  if (multiplicand < 0) {
    local_2 = 0xffff;
    multiplicand = -multiplicand;
  }
  if (multiplier < 0) {
    local_2 = ~local_2;
    multiplier = -multiplier;
  }
  uVar1 = M$MIU$LLL(multiplier,multiplicand);
  if ((short)local_2 < 0) {
    uVar1 = -uVar1;
  }
  return uVar1;
}


// Signed multiplication: long * short -> int
// Optimized signed multiply with 16-bit multiplier
// Returns lower 32 bits of product
long M$MIS$LLW(long multiplicand,short multiplier) {
  ushort uVar1;
  
  uVar1 = HIGH16(multiplicand) * multiplier;
  if (multiplier < 0) {
    uVar1 = uVar1 - LOW16(multiplicand);
  }
  return (uint)uVar1 * 0x10000 + (LOW16(multiplicand)) * (uint)(ushort)multiplier;
}


// Unsigned multiplication: ulong * ulong -> int
// Partial products algorithm (68010 lacks native 32x32 multiply)
// Computes (high2 * low1 + high1 * low2) << 16 + (low1 * low2)
// Returns lower 32 bits of 64-bit product
ulong M$MIU$LLL(ulong multiplicand,ulong multiplier) {
  return (multiplier >> 0x10) * (multiplicand & 0xffff) * 0x10000 +
         (multiplicand >> 0x10) * (multiplier & 0xffff) * 0x10000 +
         (multiplicand & 0xffff) * (multiplier & 0xffff);
}


// Unsigned multiplication: ulong * ushort -> int
// Optimized multiply with 16-bit multiplier
// Returns lower 32 bits of product
ulong M$MIU$LLW(ulong multiplicand,ushort multiplier) {
  return (multiplicand >> 0x10) * (uint)multiplier * 0x10000 +
         (multiplicand & 0xffff) * (uint)multiplier;
}

