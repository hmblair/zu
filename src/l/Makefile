CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -std=c99
OMPFLAGS ?= -fopenmp
BINDIR ?= ../../bin

# Detect platform
UNAME := $(shell uname)

ifeq ($(UNAME), Darwin)
    WATCH_SRC = watch_macos.c
    WATCH_LIBS = -framework CoreServices
else
    WATCH_SRC = watch_linux.c
    WATCH_LIBS = -lpthread
endif

all: l l-cached link-l-cache

l: l.c cache.h dir_stats.h
	$(CC) $(CFLAGS) $(OMPFLAGS) -o $(BINDIR)/l l.c

l-cached: ld.c cache.h dir_stats.h watch.h $(WATCH_SRC)
	$(CC) $(CFLAGS) $(OMPFLAGS) -o $(BINDIR)/l-cached ld.c $(WATCH_SRC) $(WATCH_LIBS) -lpthread

link-l-cache:
	chmod +x l-cache
	ln -sf ../src/l/l-cache $(BINDIR)/l-cache

clean:
	rm -f $(BINDIR)/l $(BINDIR)/l-cached $(BINDIR)/l-cache

.PHONY: all clean link-l-cache
