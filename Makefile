# Lua setup
BASE= /usr/local
LUAINC= $(BASE)/include/lua51
LUALIB= $(BASE)/lib/lua51

# basic setup
WARN= -Wall
INCS= -I$(BASE)/include -I$(LUAINC)
LIBS= -L$(BASE)/lib -L$(LUALIB) -lm -lpthread -lfcgi -llua
INSTALL_DIR= $(BASE)/bin

CC= gcc
DEBUG = -g
CFLAGS= $(INCS) $(WARN) -O2 $G
LDFLAGS= $(LIBS)

SRCS= main.c
OBJS= main.o

all: $(SRCS) luafcgid

luafcgid: $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS)

install: all
	install -b luafcgid $(INSTALL_DIR)

clean:
	rm -f $(OBJS) $(EXEC)




