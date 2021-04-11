BINDIR = bin
OBJDIR = obj
HEADERS = $(wildcard include/*.h)
COMMON_SOURCES = $(wildcard src/*.c)
SERVER_SOURCES = $(COMMON_SOURCES) $(wildcard src/server/*.c) $(wildcard src/server/io/*.c)
SERVER_OBJS = $(patsubst %.c,$(OBJDIR)/%.o,$(SERVER_SOURCES))
CLIENT_SOURCES = $(COMMON_SOURCES) $(wildcard src/client/*.c)
CLIENT_OBJS = $(patsubst %.c,$(OBJDIR)/%.o,$(CLIENT_SOURCES))
# CFLAGS= $(EXTRA_CFLAGS) -I$(CURDIR) -g # -Werror
CFLAGS += -I$(CURDIR) -g -DHAVE_KERNCALL -I$(PRIVBOX_KERN_HEADERS)/include/
LDFLAGS += -static -L$(CURDIR)

INSTR_CFLAGS ?= -mllvm -enable-priv-san

SERVER_TARGET = bin/server
CLIENT_TARGET = bin/client

all: $(SERVER_TARGET) $(CLIENT_TARGET)

$(OBJDIR)/%.o: %.c $(HEADERS)
	mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS) $< -o $@

$(OBJDIR)/src/server/io/%.o: src/server/io/%.c $(HEADERS)
	mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS) $(INSTR_CFLAGS) $< -o $@

$(SERVER_TARGET): $(SERVER_OBJS)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $^ -o $@ -lpthread -lcrypto

$(CLIENT_TARGET): $(CLIENT_OBJS)
	mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) $^ -o $@ -lpthread -lcrypto

clean:
	rm -fr $(BINDIR) $(OBJDIR)
