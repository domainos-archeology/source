
SRCS := \
  $(wildcard cal/*.c) \
  $(wildcard math/*.c) \
  $(wildcard term/*.c)
OBJS := $(patsubst %.c,%.o,$(SRCS))

# we assume local #include's will have fully qualified paths
CFLAGS=-I.

domain_os: $(OBJS)
	$(CC) -o $@ $(OBJS)