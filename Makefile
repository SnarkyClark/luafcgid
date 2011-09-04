BASE = /usr/local

# Lua 5.1
LUAINC = $(BASE)/include/lua51
LUALIB = $(BASE)/lib/lua51
LLIB = lua
LUA_PACKAGEPATH = $(BASE)/share/lua/5.1/

## LuaJIT2
#LUAINC = $(BASE)/include/luajit-2.0
#LUALIB = $(BASE)/lib
#LLIB = luajit-5.1

# basic setup
CC = gcc
WARN = -Wall
INCS = -I$(BASE)/include -I$(LUAINC)
LIBS = -L$(BASE)/lib -L$(LUALIB) -lm -lpthread -lfcgi -l$(LLIB)
DEBUG = -ggdb 
OPTS = -O2
#OPTS = -O3 -march=native
CFLAGS = $(INCS) $(WARN) $(OPTS) $(DEBUG) $G
LDFLAGS = $(LIBS) $(OPTS) $(DEBUG)
INSTALL_DIR = $(BASE)/bin

SOURCES = main.c config.c pool.c buffer.c request.c
OBJECTS = $(SOURCES:.c=.o)
EXEC = luafcgid
	
all: $(SOURCES) $(EXEC)

$(EXEC): $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) -o $@

install: all
	install -b $(EXEC) $(INSTALL_DIR)
	install -b luafcgid.lua $(LUA_PACKAGEPATH)

clean:
	rm -f $(OBJECTS) $(EXEC)




