# Calling convention of the hand-assembled routines (source-nxtd)

The `<subsystem>/sau2/*.s` files are the 10.2 image's hand-written assembly,
byte for byte (`make check` compares every section with the image).  They
read their arguments straight off SP at the offsets **Domain Pascal** laid
out, and Domain Pascal does not lay out a frame the way gcc does.

## The two layouts

| parameter | Domain Pascal pushes | gcc pushes |
|---|---|---|
| 32-bit value, pointer, `var` | 4 bytes (`move.l` / `pea`) | 4 bytes |
| 16-bit value | **2 bytes** (`move.w x,-(sp)`) | 4 bytes, the value in the low (higher-addressed) half |
| 8-bit value | 2 bytes, the byte in the word's low half | 4 bytes |

Arguments go right to left in both, so argument 1 sits at `(4,SP)` on entry.
When a frame's total is not a multiple of 4, the Pascal caller pads with a
`subq.l #2,sp` before the first push, so the pad lies *after* (above) the
last word, never between parameters.  So:

* a word that is **alone in its 4-byte slot** (followed by the pad, or by
  another 4-byte-aligned group) is at the slot's **first** word;
* **two adjacent words** fill one 4-byte slot, the first parameter at the
  lower address;
* a word followed directly by a longword makes that longword **straddle**
  gcc's slots (e.g. `IO_$TRAP`: `(4)` vector.w, `(6)` handler.l).

Example, ML_$SPIN_UNLOCK (0xE20BBE, `46 EF 00 08 4E 75`):
`move.w (8,sp),sr`.  The image's callers push
`subq.l #2,sp; move.w token,-(sp); pea lock`.  gcc called `f(lock, token)`
puts the token at `(10,sp)` and the word at `(8,sp)` is 0: SR := 0, user
mode, the first boot stop (rfc-cold-start.md section 10, run 2).

## What the C side does

The asm stays.  `arch/arch.h` packs values into the Pascal layout, the same
on every architecture (it is a value, not a memory layout, so host tests
exercise it):

```c
ARCH_PASCAL_WORD_SLOT(w)          /* w << 16: a lone word (or byte)     */
ARCH_PASCAL_WORD_PAIR_SLOT(a, b)  /* a << 16 | b: two adjacent words    */
ARCH_PASCAL_SLOT_WORD(s), ARCH_PASCAL_SLOT_WORD2(s)   /* unpack          */
ARCH_PASCAL_SLOTS_LONG(s1, s2)    /* a longword straddling two slots    */
```

The owner's public header declares each routine with `uint32_t <param>_slot`
parameters and, right below, a same-name function-like macro that packs the
natural arguments, so callers are unchanged:

```c
void ML_$SPIN_UNLOCK(void *lockp, uint32_t token_slot);
#define ML_$SPIN_UNLOCK(lockp, token) \
    (ML_$SPIN_UNLOCK)((lockp), ARCH_PASCAL_WORD_SLOT(token))
```

A C definition (host model, test stub) parenthesises the name to defeat the
macro and unpacks: `void (ML_$SPIN_UNLOCK)(void *lockp, uint32_t token_slot)`.
Each header comment gives the routine's Pascal frame, an image call site,
and the gcc slots it maps to.  A straddling pointer travels as a 32-bit VA
(`ARCH_PTR_TO_VA`); on a 64-bit host a test must keep the pointee inside an
`ARCH_HOST_VA_BASE` window.  Where a longword straddles slots
(MMU_$INSTALL_LIST, PROC1_$GET_INFO_INT, IO_$TRAP) the macro calls a
`static inline` helper in the same header (`mmu_$install_list_slots`,
`proc1_$get_info_int_slots`, `io_$trap_slots`) so each argument is
evaluated once.

The reverse direction needs the same treatment: a **C** function that hand
assembly calls with `move.w` pushes takes the slot too (PROC1_$SET_TS,
called from ec/sau2/advance_int.s).  PROC1_$UNBIND and DXM_$ADD_SIGNAL still
need it (source-o0gn).

## Routines (frame offsets from SP at entry)

| routine | Pascal frame | gcc slots |
|---|---|---|
| ML_$SPIN_UNLOCK | 4 lockp, 8 token.w | lockp, WORD(token) |
| MMU_$REMOVE_LIST | 4 array, 8 count.w | array, WORD(count) |
| MMU_$REMOVE_VIRTUAL | 4 va, 8 count.w, A asid.w, C array, 10 &n | va, PAIR(count,asid), array, &n |
| MMU_$REMOVE_ASID, MMU_$INSTALL_ASID, MMU_$MCR_CHANGE | 4 x.w | WORD(x) |
| MMU_$SET_CSR | 4 csr.w (reads byte 5) | WORD(csr) |
| MMU_$SET_PROT | 4 ppn, 8 prot.w | ppn, WORD(prot) |
| MMU_$INSTALL, MMU_$INSTALL_PRIVATE | 4 ppn, 8 va, C asid.w, E prot.w | ppn, va, PAIR(asid,prot) |
| MMU_$INSTALL_LIST | 4 count.w, 6 array, A va, E asid.w, 10 prot.w | PAIR(count,array.hi), PAIR(array.lo,va.hi), PAIR(va.lo,asid), WORD(prot) |
| PROC1_$SET_LOCK, PROC1_$CLR_LOCK | 4 lock_id.w | WORD(lock_id) |
| PROC1_$GET_INFO_INT | 4 pid.w, then six unpadded longwords | 7 slots, each longword straddling |
| FIM_$FP_INIT, FIM_$GET_USER_SR_PTR, FIM_$DELIVER/CLEAR_TRACE_FAULT | 4 x.w | WORD(x) |
| FP_$GET_FP, FP_$PUT_FP | 4 asid.w | WORD(asid) |
| DISP_LITES | 4 pattern.w, 6 y.w | PAIR(pattern,y) |
| XNS_IDP_$CHECKSUM | 4 data, 8 count.w | data, WORD(count) |
| XNS_IDP_$HOP_AND_SUM | 4 sum.w, 6 hop.w | PAIR(sum,hop) |
| IO_$TRAP | 4 vector.w, 6 handler | PAIR(vector,handler.hi), WORD(handler.lo) |
| PROC1_$SET_TS (C, called from asm) | 8(A6) pcb, C(A6) ts.w | pcb, WORD(ts) |

**Not packed:** MMU_$CLR_USED reads `move.w (4,sp)`, but both image callers
push the ppn with `move.l`, so the image indexes the PFT by the ppn's high
word; C passes the longword and reproduces that (source-qhu6).  The reads
in io/sau2/use_int_stack.s, misc/sau2/crash_system.s, fim/sau2/bus_err.s,
parity_trap.s, fsave.s, exit.s, proc1/sau2/int_handler.s and
stop/sau2/watch.s are exception frames or longword slots.
