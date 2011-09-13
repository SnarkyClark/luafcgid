SHELL = /bin/sh

.SUFFIXES:
.SUFFIXES: .c .o

# Common prefix 
PREFIX = /usr/local
# Directory in which to put the executable
BINDIR = $(PREFIX)/bin
# Directory in which to put runtime configuration
CONFDIR = $(PREFIX)/etc
# Directory in which to put rc.d/initd scripts
INITDIR = $(PREFIX)/etc/rc.d
# Directory in which to put Lua modules
PACKAGEPATH = $(PREFIX)/share/lua/5.1

# Lua 5.1 config
LUAINC = $(PREFIX)/include/lua51
LUALIB = $(PREFIX)/lib/lua51
LLIB = lua

## LuaJIT2
#LUAINC = $(prefix)/include/luajit-2.0
#LUALIB = $(prefix)/lib
#LLIB = luajit-5.1

SRCDIR = src
OBJDIR = obj

# basic setup
CC = gcc
WARN = -Wall
INCS = -I./$(SRCDIR) -I$(PREFIX)/include -I$(LUAINC)
LIBS = -L$(PREFIX)/lib -L$(LUALIB) -lm -lpthread -lfcgi -l$(LLIB)
#DEBUG = -ggdb 
OPTS = -O2
#OPTS = -O3 -march=native
CFLAGS = $(INCS) $(WARN) $(OPTS) $(DEBUG) $G
LDFLAGS = $(LIBS) $(OPTS) $(DEBUG)

VPATH = ../$(SRCDIR)

SOURCES = main.c config.c pool.c buffer.c request.c
OBJECTS = $(SOURCES:%.c=%.o)
EXEC = luafcgid
	
all: $(SOURCES) $(EXEC)

$(EXEC): $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) -o $@

install: all
	install -b $(EXEC) $(BINDIR)
	@mkdir -p $(PACKAGEPATH)
	install -b ../scripts/luafcgid.lua $(PACKAGEPATH)/luafcgid.lua
	@mkdir -p $(CONFDIR)/luafcgid
	install -b ../scripts/etc/config.lua $(CONFDIR)/luafcgid/config.lua
	install -b ../scripts/etc/rc.d/luafcgid $(INITDIR)/luafcgid

clean:
	rm -f $(OBJECTS) $(EXEC)




