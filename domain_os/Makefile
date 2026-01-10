
SRCS := $(wildcard math/*.c)
OBJS := $(patsubst %.c,%.o,$(SRCS))

domain_os: $(OBJS)
	echo $(SRCS)
	$(CC) -o $@ $<