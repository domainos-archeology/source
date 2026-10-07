
TARGET_SAU := sau2

# List of all subsystems
SUBSYSTEMS := acl app area as asknode arch ast audit base bat cache cal chksum cold ct dbuf di dir disk dma dtty dump dxm ec file fim flop flp fp gpu fm hint iic io kbd log lpr mac mac_os math mem misc ml mmap mmu msg mst mt name net netlog net_io netbuf network os osinfo pacct parity pas pbu pchist peb pkt pmap proc1 proc2 rem_file rem_name rgyc ring rip route scsi sio sio2681 sio6509 smd sock stop suma svc sysboot sysbus tape term time timer tone tpad tty uid vfmt volx vtoc win wp xns xpd

WARNING_FLAGS := -Werror

SAU2_FLAGS := -DARCH_M68K -DCPU_M68020 -DSAU2 -DREVERSE_MAPPED_MMU -DHAS_TOKEN_RING -mcpu=68020 -nostdlib -ffreestanding -fno-builtin -fno-stack-protector

# we assume local #include's will have fully qualified paths
# -ffunction-sections / -fdata-sections: every C function is its own input
# section `.text.<symbol>' (and every datum `.rodata/.data/.bss.<symbol>'), so
# the generated layout.ld can put the code in the SAU2 link map's order
# (tools/gen_layout_ld.py).
CFLAGS := -std=c23 -I. -O2 $(SAU2_FLAGS) $(WARNING_FLAGS) -ffunction-sections -fdata-sections

CC := m68k-elf-gcc

LD := m68k-elf-ld
LDFLAGS := -T sau2.ld

AR := m68k-elf-ar
ARFLAGS := rcs

BUILD_DIR := build/$(TARGET_SAU)
DIST_DIR := dist/$(TARGET_SAU)

# Link-order fragment (see the layout rules below).  SAU2_MAP is the link map
# of the exact SR10.2 SAU2 image in Ghidra; it lives outside this repository.
SAU2_MAP ?= $(HOME)/src/domainos-archeology/sau2-maps/domain_os.10.2.map
LAYOUT_GEN := tools/gen_layout_ld.py
LAYOUT_LD := $(BUILD_DIR)/layout.ld
LAYOUT_CHECK_ELF := $(BUILD_DIR)/layout-check.elf

# Host-side unit tests.  Each <subsystem>/test/test_<function>.c is a
# self-contained program (it #includes the .c under test directly) built
# with the host compiler and run natively.
HOST_CC := cc
HOST_CFLAGS := -std=gnu2x -DARCH_HOST -I. -g -O0 -Wall -Wno-unused-function -Wno-unused-variable
TEST_BUILD_DIR := build/host/test
TEST_SRCS := $(wildcard $(addsuffix /test/test_*.c,$(SUBSYSTEMS)))
TEST_BINS := $(patsubst %.c,$(TEST_BUILD_DIR)/%,$(TEST_SRCS))

# Generate library paths for all subsystems
SUBSYSTEM_LIBS := $(foreach sub,$(SUBSYSTEMS),$(BUILD_DIR)/lib$(sub).a)

ALL_SUBSYSTEM_OBJS :=

.PHONY: all clean domain_os test test-build clean-test check check-layout check-asm check-guards check-iodefs

all:: domain_os

# Define per-subsystem rules using a template
define SUBSYSTEM_template
$(1)_SRCS := $$(wildcard $(1)/*.c)
$(1)_ASM_SRCS := $$(wildcard $(1)/$$(TARGET_SAU)/*.s)
$(1)_OBJS := $$(patsubst %.c,$$(BUILD_DIR)/%.o,$$($(1)_SRCS))
$(1)_ASM_OBJS := $$(patsubst %.s,$$(BUILD_DIR)/%.o,$$($(1)_ASM_SRCS))

ALL_SUBSYSTEM_OBJS += $$($(1)_OBJS) $$($(1)_ASM_OBJS)

.PHONY: $(1) clean-$(1)

# Phony target for building just this subsystem
$(1): $$($(1)_OBJS) $$($(1)_ASM_OBJS)

clean-$(1):
	@echo "[RM] $(1)_OBJS $(1)_ASM_OBJS"
	@rm -f $$($(1)_OBJS) $$($(1)_ASM_OBJS)
endef

# Instantiate the template for each subsystem
$(foreach sub,$(SUBSYSTEMS),$(eval $(call SUBSYSTEM_template,$(sub))))

# arch/m68k/*.c: compiler-support routines for the m68k target only (memcpy;
# the kernel links no libc).  Not image code, so no map symbol: they go to
# the layout's catch-all.  Host test builds use libc instead.
ARCH_M68K_SRCS := $(wildcard arch/m68k/*.c)
ARCH_M68K_OBJS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(ARCH_M68K_SRCS))
ALL_SUBSYSTEM_OBJS += $(ARCH_M68K_OBJS)
arch: $(ARCH_M68K_OBJS)

# arch/m68k/sau2/*.s: per-SAU objects with no bytes (iodefs.s: the map's
# absolute symbols - I/O pages, PTT, PROT, TRAP_PAGE - that the hand-written
# cold-start code references by name so its cells are relocations).
ARCH_M68K_SAU_ASM_SRCS := $(wildcard arch/m68k/$(TARGET_SAU)/*.s)
ARCH_M68K_SAU_ASM_OBJS := $(patsubst %.s,$(BUILD_DIR)/%.o,$(ARCH_M68K_SAU_ASM_SRCS))
ALL_SUBSYSTEM_OBJS += $(ARCH_M68K_SAU_ASM_OBJS)
arch: $(ARCH_M68K_SAU_ASM_OBJS)

# svc/sau2/*.s share instruction-encoding macros through a gas .include
# (svc/sau2/svc_macros.inc, resolved relative to this directory).  The
# generic %.o: %.s rule cannot see that, so state the dependency here.
$(svc_ASM_OBJS): svc/$(TARGET_SAU)/svc_macros.inc

# Main target
domain_os: $(DIST_DIR)/domain_os

$(DIST_DIR)/domain_os: $(ALL_SUBSYSTEM_OBJS) sau2.ld $(LAYOUT_LD)
	@mkdir -p $(dir $@)
	@echo "[LINK] $@"
	@$(LD) -L $(BUILD_DIR) $(LDFLAGS) -o $@ $(ALL_SUBSYSTEM_OBJS)

# Link order (source-0i3; the owner's decision of 2026-09-28).  Code and
# module data blocks are linked in the order of the SAU2 link map, not at
# fixed addresses.  sau2.ld INCLUDEs layout.ld inside its `.text' output
# section: one input-section statement per C function (-ffunction-sections),
# hand-written routine and MODULE_DATA block, in map order, then the
# catch-all.  tools/gen_layout_ld.py writes it from the map, the compiled
# objects' symbol tables and the MODULE_DATA_DEFINE sites (whose addresses
# are now ordering keys), and refuses odd or non-literal block addresses and
# duplicate block names.  It is regenerated before every link.
$(LAYOUT_LD): $(ALL_SUBSYSTEM_OBJS) $(LAYOUT_GEN) $(SAU2_MAP)
	@mkdir -p $(dir $@)
	@echo "[GEN] $@"
	@python3 $(LAYOUT_GEN) --map $(SAU2_MAP) -o $@ $(ALL_SUBSYSTEM_OBJS)

# `make check': static checks plus a scratch link.  The real link still has
# hundreds of unresolved symbols, so the check links an ELF with
# --unresolved-symbols=ignore-all and then verifies with m68k-elf-nm that
# every placed map symbol and block appears in map order, reporting the
# first inversion, the number placed and what fell into the catch-all.  (The
# ELF has one RWX segment because the module data blocks now sit among the
# code in `.text'; the RFC image is flat binary, so that is expected.)
check: check-layout check-asm check-guards check-iodefs

check-layout: $(LAYOUT_LD) $(ALL_SUBSYSTEM_OBJS) sau2.ld
	@echo "[CHECK] layout: static checks"
	@python3 $(LAYOUT_GEN) --map $(SAU2_MAP) --check-only $(ALL_SUBSYSTEM_OBJS)
	@echo "[CHECK] layout: scratch link $(LAYOUT_CHECK_ELF)"
	@$(LD) -L $(BUILD_DIR) $(LDFLAGS) --oformat elf32-m68k --no-warn-rwx-segments \
	    --unresolved-symbols=ignore-all -o $(LAYOUT_CHECK_ELF) $(ALL_SUBSYSTEM_OBJS)
	@echo "[CHECK] layout: map order"
	@python3 $(LAYOUT_GEN) --map $(SAU2_MAP) --check-elf $(LAYOUT_CHECK_ELF) $(ALL_SUBSYSTEM_OBJS)

# Relocation-aware byte gate for the hand-written assembly (source-6psc):
# every <subsystem>/sau2/*.s object is compared run by run with the SAU2
# image bytes in tools/asm_image_ref.txt (checked in, extracted once from
# Ghidra with `asm_compare.py --extract'), with the bytes under each
# relocation set aside and every relocated operand resolved against the
# image operand: encodings identical to the image; address operands resolve
# to our objects.  Documented widenings and deviations are listed by the
# script.
ALL_ASM_OBJS = $(foreach sub,$(SUBSYSTEMS),$($(sub)_ASM_OBJS))
ASM_COMPARE := tools/asm_compare.py
ASM_IMAGE_REF := tools/asm_image_ref.txt

check-asm: $(ALL_ASM_OBJS) $(ASM_COMPARE) $(ASM_IMAGE_REF)
	@echo "[CHECK] asm: relocation-aware compare with the image"
	@python3 $(ASM_COMPARE) --map $(SAU2_MAP) --ref $(ASM_IMAGE_REF) $(ALL_ASM_OBJS)

# The regions the kernel's Ghidra program does not hold (the RFC header and
# COLD_START, file 0x000..0x800 = 0x101400; the DUMP page, file 0x800..0xC00
# = VA 0xE00400) take their reference bytes from the image file itself:
# `make asm-ref-image' appends the runs of those regions that the reference
# file lacks (source-gfn1).  Like --extract, a maintenance step: its output
# is checked in.
SAU2_IMAGE ?= $(HOME)/src/domainos-archeology/sr10.2-install/install/ri.apollo.os.v.10.2/sau2/domain_os
.PHONY: asm-ref-image
asm-ref-image: $(ALL_ASM_OBJS) $(ASM_COMPARE)
	@python3 $(ASM_COMPARE) --map $(SAU2_MAP) --ref $(ASM_IMAGE_REF) \
	    --extract-file $(SAU2_IMAGE):0x101400:0x0-0x800 \
	    --extract-file $(SAU2_IMAGE):0xDFFC00:0x800-0xC00 $(ALL_ASM_OBJS)

# Architecture guards around data (source-702z, the last step of
# docs/design-per-process-data.md): outside arch/, an #if/#ifdef on
# ARCH_M68K or ARCH_HOST may not hold an object declaration or definition,
# an extern, a #define that casts an absolute address to a pointer, or a
# struct/union definition.  Pointer-layout _Static_asserts, host fallback
# bodies of hand-written routines and .text section macros are allowed.  The
# guards still left for tracked beads are listed, with the bead, in the
# script's EXEMPT table; tools/arch_m68k_guards.md classifies every guard.
GUARD_CHECK := tools/check_guards.py

check-guards: $(GUARD_CHECK)
	@echo "[CHECK] guards: no architecture guards around data"
	@python3 $(GUARD_CHECK)

# arch/m68k/sau2/iodefs.s spells the I/O page, PTT, PROT and TRAP_PAGE
# addresses a second time, as link-time absolute symbols for the hand-written
# cold-start code; the C headers (arch/m68k/sau2/hw.h, svc/svc.h) are the
# source of truth, and this check keeps the two equal (source-gfn1 review).
IODEFS_CHECK := tools/check_iodefs.py

check-iodefs: $(IODEFS_CHECK) arch/m68k/$(TARGET_SAU)/iodefs.s arch/m68k/$(TARGET_SAU)/hw.h svc/svc.h
	@echo "[CHECK] iodefs: arch/m68k/$(TARGET_SAU)/iodefs.s agrees with hw.h"
	@python3 $(IODEFS_CHECK)

# Generic C compilation rule
$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "[CC] $@"
	@$(CC) $(CFLAGS) -c -o $@ $<

# Generic assembly compilation rule
$(BUILD_DIR)/%.o: %.s
	@mkdir -p $(dir $@)
	@echo "[AS] $<"
	@$(CC) $(SAU2_FLAGS) -c -o $@ $<

# Unit tests (host build)
# Tests #include the .c under test and many headers; rebuild when any header changes.
ALL_HEADERS := $(wildcard */*.h */*/*.h)
$(TEST_BUILD_DIR)/%: %.c $(ALL_HEADERS)
	@mkdir -p $(dir $@)
	@echo "[HOSTCC] $<"
	@$(HOST_CC) $(HOST_CFLAGS) -I$(dir $<) -o $@ $<

test-build: $(TEST_BINS)

test: $(TEST_BINS)
	@fail=0; for t in $(TEST_BINS); do \
	  echo "[TEST] $$t"; \
	  if ! (cd $$(dirname $$t) && ./$$(basename $$t)) > $$t.log 2>&1; then \
	    echo "  FAILED: $$t (see $$t.log)"; fail=$$((fail+1)); \
	  fi; \
	done; \
	echo "$$(echo $(TEST_BINS) | wc -w | tr -d ' ') tests, $$fail failed"; \
	[ $$fail -eq 0 ]

clean-test:
	rm -rf $(TEST_BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR) $(DIST_DIR) $(TEST_BUILD_DIR)
