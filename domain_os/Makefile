
TARGET_SAU := sau2

# List of all subsystems
SUBSYSTEMS := acl app area as asknode arch ast audit base bat cache cal dbuf di dir disk dma dtty dxm ec file fim flop flp fp gpu fm hint iic io kbd log mac mac_os math mem misc ml mmap mmu msg mst name net netlog net_io netbuf network os osinfo pacct parity pas pbu pchist peb pkt pmap proc1 proc2 rem_file rgyc ring rip route scsi sio sio2681 smd sock stop suma svc sysboot sysbus tape term time timer tone tpad tty uid vfmt volx vtoc win wp xns xpd

WARNING_FLAGS := -Werror -Wno-error=incompatible-pointer-types -Wno-error=int-conversion

SAU2_FLAGS := -DARCH_M68K -DCPU_M68020 -DSAU2 -DREVERSE_MAPPED_MMU -DHAS_TOKEN_RING -mcpu=68020 -nostdlib -ffreestanding -fno-builtin -fno-stack-protector

# we assume local #include's will have fully qualified paths
CFLAGS := -std=c23 -I. -O2 $(SAU2_FLAGS) $(WARNING_FLAGS)

CC := m68k-elf-gcc

LD := m68k-elf-ld
LDFLAGS := -T sau2.ld

AR := m68k-elf-ar
ARFLAGS := rcs

BUILD_DIR := build/$(TARGET_SAU)
DIST_DIR := dist/$(TARGET_SAU)

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

.PHONY: all clean domain_os test test-build clean-test

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

# Main target
domain_os: $(DIST_DIR)/domain_os

$(DIST_DIR)/domain_os: $(ALL_SUBSYSTEM_OBJS) sau2.ld
	@mkdir -p $(dir $@)
	@echo "[LINK] $@"
	@$(LD) $(LDFLAGS) -o $@ $(ALL_SUBSYSTEM_OBJS)

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
