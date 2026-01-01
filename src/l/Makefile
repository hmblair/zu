# Makefile for l directory listing tool (refactored)

CC = cc
CFLAGS = -O2 -Wall -Wextra -std=c99
LIBS = -lsqlite3 -lpthread

# OpenMP support (use homebrew clang on macOS)
UNAME := $(shell uname)
ifeq ($(UNAME),Darwin)
  # Check for homebrew llvm
  BREW_LLVM := $(shell brew --prefix llvm 2>/dev/null)
  ifneq ($(BREW_LLVM),)
    CC = $(BREW_LLVM)/bin/clang
    CFLAGS += -fopenmp
    LIBS += -L$(BREW_LLVM)/lib -lomp
  endif
else
  CFLAGS += -fopenmp
endif

# Optional libgit2 support
HAVE_LIBGIT2 := $(shell pkg-config --exists libgit2 2>/dev/null && echo yes)
ifeq ($(HAVE_LIBGIT2),yes)
  CFLAGS += -DHAVE_LIBGIT2 $(shell pkg-config --cflags libgit2)
  LIBS += $(shell pkg-config --libs libgit2)
endif

# Debug build
ifdef DEBUG
  CFLAGS = -g -O0 -Wall -Wextra -std=c99 -fsanitize=address,undefined -DDEBUG
  CFLAGS += -fopenmp
  ifeq ($(HAVE_LIBGIT2),yes)
    CFLAGS += -DHAVE_LIBGIT2 $(shell pkg-config --cflags libgit2)
  endif
endif

# Object files
COMMON_OBJS = common.o
CACHE_CLIENT_OBJS = cache.o
CACHE_DAEMON_OBJS = cache_daemon.o
GIT_OBJS = git.o
UI_OBJS = ui.o
DAEMON_OBJS = daemon.o

# Main targets
all: l l-cached

l: l.o $(COMMON_OBJS) $(CACHE_CLIENT_OBJS) $(GIT_OBJS) $(UI_OBJS) $(DAEMON_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

l-cached: ld.o $(COMMON_OBJS) $(CACHE_DAEMON_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

# Object files
common.o: common.c common.h
	$(CC) $(CFLAGS) -c -o $@ $<

cache.o: cache.c cache.h common.h
	$(CC) $(CFLAGS) -c -o $@ $<

cache_daemon.o: cache_daemon.c cache.h common.h
	$(CC) $(CFLAGS) -c -o $@ $<

git.o: git.c git.h common.h
	$(CC) $(CFLAGS) -c -o $@ $<

ui.o: ui.c ui.h cache.h git.h common.h
	$(CC) $(CFLAGS) -c -o $@ $<

l.o: l.c common.h cache.h git.h ui.h daemon.h
	$(CC) $(CFLAGS) -c -o $@ $<

daemon.o: daemon.c daemon.h common.h
	$(CC) $(CFLAGS) -c -o $@ $<

ld.o: ld.c common.h cache.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.o l l-cached

.PHONY: all clean
