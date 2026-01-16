
TARGET_SAU := sau2

SRCS := \
  $(wildcard ast/*.c) \
  $(wildcard bat/*.c) \
  $(wildcard cal/*.c) \
  $(wildcard ec/*.c) \
  $(wildcard math/*.c) \
  $(wildcard misc/*.c) \
  $(wildcard ml/*.c) \
  $(wildcard mmap/*.c) \
  $(wildcard mmu/*.c) \
  $(wildcard os/*.c) \
  $(wildcard osinfo/*.c) \
  $(wildcard pmap/*.c) \
  $(wildcard proc1/*.c) \
  $(wildcard proc2/*.c) \
  $(wildcard smd/*.c) \
  $(wildcard term/*.c) \
  $(wildcard time/*.c) \
  $(wildcard tpad/*.c) \
  $(wildcard tty/*.c) \
  $(wildcard uid/*.c)

# Assembly files (platform-specific)
ASM_SRCS := $(wildcard ec/$(TARGET_SAU)/*.s) $(wildcard ml/$(TARGET_SAU)/*.s) $(wildcard proc1/$(TARGET_SAU)/*.s) $(wildcard smd/$(TARGET_SAU)/*.s)
ASM_OBJS := $(patsubst %.s,build/$(TARGET_SAU)/%.o,$(ASM_SRCS))
OBJS := $(patsubst %.c,build/$(TARGET_SAU)/%.o,$(SRCS))

# we assume local #include's will have fully qualified paths
CFLAGS=-I. -O2 $(SAU2_FLAGS)

.PHONY: all clean domain_os dist-dir

CC := m68k-elf-gcc
SAU2_FLAGS := -DM68K -DM68010 -DSAU2 -DREVERSE_MAPPED_MMU -DHAS_TOKEN_RING -mcpu=68010 -nostdlib -ffreestanding -fno-builtin -fno-stack-protector

LD := m68k-elf-gcc
LDFLAGS := -nostdlib -ffreestanding

all:: domain_os

clean:
	rm -f $(OBJS) dist/$(TARGET_SAU)/domain_os

domain_os: make-build-dirs make-dist-dir dist/$(TARGET_SAU)/domain_os

dist/$(TARGET_SAU)/domain_os: $(OBJS) $(ASM_OBJS)
	@mkdir -p $(dir $@)
	@echo "[LINK] $@"
	$(LD) $(LDFLAGS) -o $@ $(OBJS) $(ASM_OBJS)

build/$(TARGET_SAU)/%.o : %.c
	@echo "[CC] $@"
	@$(CC) $(CFLAGS) -c -o $@ $<

build/$(TARGET_SAU)/ec/$(TARGET_SAU)/%.o: ec/$(TARGET_SAU)/%.s
	@mkdir -p $(dir $@)
	@echo "[AS] $<"
	@$(CC) $(SAU2_FLAGS) -c -o $@ $<

build/$(TARGET_SAU)/ml/$(TARGET_SAU)/%.o: ml/$(TARGET_SAU)/%.s
	@mkdir -p $(dir $@)
	@echo "[AS] $<"
	@$(CC) $(SAU2_FLAGS) -c -o $@ $<

build/$(TARGET_SAU)/proc1/$(TARGET_SAU)/%.o: proc1/$(TARGET_SAU)/%.s
	@mkdir -p $(dir $@)
	@echo "[AS] $<"
	@$(CC) $(SAU2_FLAGS) -c -o $@ $<

build/$(TARGET_SAU)/smd/$(TARGET_SAU)/%.o: smd/$(TARGET_SAU)/%.s
	@mkdir -p $(dir $@)
	@echo "[AS] $<"
	@$(CC) $(SAU2_FLAGS) -c -o $@ $<

make-build-dirs:
	@mkdir -p $(dir $(OBJS)) $(dir $(ASM_OBJS))

make-dist-dir:
	@mkdir -p dist/$(TARGET_SAU)
