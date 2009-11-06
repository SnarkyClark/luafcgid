# Lua setup
BASE = /usr/local
LUAINC = $(BASE)/include/lua51
LUALIB = $(BASE)/lib/lua51

# basic setup
CC = gcc
WARN = -Wall
INCS = -I$(BASE)/include -I$(LUAINC)
LIBS = -L$(BASE)/lib -L$(LUALIB) -lm -lpthread -lfcgi -llua
INSTALL_DIR = $(BASE)/bin
#DEBUG = -ggdb
OPTS = -O2
CFLAGS = $(INCS) $(WARN) $(OPTS) $(DEBUG) $G
LDFLAGS = $(LIBS) $(OPTS) $(DEBUG)

SRCS = main.c config.c pool.c request.c parser.c
OBJS = main.o config.o pool.o request.o parser.o
EXEC = luafcgid

all: $(SRCS) $(EXEC)

$(EXEC): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS)

install: all
	install -b $(EXEC) $(INSTALL_DIR)

clean:
	rm -f $(OBJS) $(EXEC)




