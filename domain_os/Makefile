
TARGET_SAU := sau2

SRCS := \
  $(wildcard ast/*.c) \
  $(wildcard cal/*.c) \
  $(wildcard math/*.c) \
  $(wildcard mmap/*.c) \
  $(wildcard mmu/*.c) \
  $(wildcard os/*.c) \
  $(wildcard osinfo/*.c) \
  $(wildcard pmap/*.c) \
  $(wildcard term/*.c) \
  $(wildcard tty/*.c)
OBJS := $(patsubst %.c,build/$(TARGET_SAU)/%.o,$(SRCS))

# we assume local #include's will have fully qualified paths
CFLAGS=-I. -O2

.PHONY: all clean domain_os dist-dir

CC := m68k-elf-gcc
SAU2_FLAGS := -DM68K -DM68010 -DSAU2 -DREVERSE_MAPPED_MMU -DHAS_TOKEN_RING --target=m68k-unknown-linux -mcpu=68010 -nostdlib -ffreestanding -fno-builtin -fno-stack-protector

all:: domain_os

clean:
	rm -f $(OBJS) dist/$(TARGET_SAU)/domain_os

domain_os: make-build-dirs make-dist-dir dist/$(TARGET_SAU)/domain_os

dist/$(TARGET_SAU)/domain_os: $(OBJS)
	@mkdir -p $(dir $@)
	@echo "[LINK] $@"
	@$(LD) $(LDFLAGS) -o $@ $(OBJS)

$(OBJS): %.o: $(patsubst build/$(TARGET_SAU)/%.o,%.c,$(OBJS))
	@echo "[CC] $@" 
	@$(CC) $(CFLAGS) -c -o $@ $<

make-build-dirs:
	@mkdir -p $(dir $(OBJS))

make-dist-dir:
	@mkdir -p dist/$(TARGET_SAU)
