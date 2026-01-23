
TARGET_SAU := sau2

# List of all subsystems
SUBSYSTEMS := acl app area as asknode ast audit bat cache cal dbuf di disk dma dtty dxm ec file fim flop fp gpu fm hint iic log mac math mem misc ml mmap mmu msg name net netlog net_io netbuf network os osinfo pacct parity pas pbu pchist peb pkt pmap proc1 proc2 rem_file rgyc ring rip route scsi sio sio2681 smd sock stop term time tone tpad tty uid vfmt volx vtoc vtoce xns

# we assume local #include's will have fully qualified paths
CFLAGS := -I. -O2 $(SAU2_FLAGS)

CC := m68k-elf-gcc
SAU2_FLAGS := -DM68K -DM68010 -DSAU2 -DREVERSE_MAPPED_MMU -DHAS_TOKEN_RING -mcpu=68010 -nostdlib -ffreestanding -fno-builtin -fno-stack-protector

LD := m68k-elf-ld
LDFLAGS := -T sau2.ld

AR := m68k-elf-ar
ARFLAGS := rcs

BUILD_DIR := build/$(TARGET_SAU)
DIST_DIR := dist/$(TARGET_SAU)

# Generate library paths for all subsystems
SUBSYSTEM_LIBS := $(foreach sub,$(SUBSYSTEMS),$(BUILD_DIR)/lib$(sub).a)

.PHONY: all clean domain_os

all:: domain_os

# Define per-subsystem rules using a template
define SUBSYSTEM_template
$(1)_LIB := $$(BUILD_DIR)/lib$(1).a
$(1)_SRCS := $$(wildcard $(1)/*.c)
$(1)_ASM_SRCS := $$(wildcard $(1)/$$(TARGET_SAU)/*.s)
$(1)_OBJS := $$(patsubst %.c,$$(BUILD_DIR)/%.o,$$($(1)_SRCS))
$(1)_ASM_OBJS := $$(patsubst %.s,$$(BUILD_DIR)/%.o,$$($(1)_ASM_SRCS))

.PHONY: $(1) clean-$(1)

# Phony target for building just this subsystem
$(1): $$($(1)_LIB)

clean-$(1):
	@echo "[RM] $(1)_LIB $(1)_OBJS $(1)_ASM_OBJS"
	@rm -f $$($(1)_LIB) $$($(1)_OBJS) $$($(1)_ASM_OBJS)

# The archive depends on all objects
$$($(1)_LIB): $$($(1)_OBJS) $$($(1)_ASM_OBJS)
	@mkdir -p $$(dir $$@)
	@echo "[AR] $$@"
	@$$(AR) $$(ARFLAGS) $$@ $$^
endef

# Instantiate the template for each subsystem
$(foreach sub,$(SUBSYSTEMS),$(eval $(call SUBSYSTEM_template,$(sub))))

# Main target
domain_os: $(DIST_DIR)/domain_os

$(DIST_DIR)/domain_os: $(SUBSYSTEM_LIBS) sau2.ld
	@mkdir -p $(dir $@)
	@echo "[LINK] $@"
	$(LD) $(LDFLAGS) -o $@ --whole-archive $(SUBSYSTEM_LIBS) --no-whole-archive

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

clean:
	rm -rf $(BUILD_DIR) $(DIST_DIR)
