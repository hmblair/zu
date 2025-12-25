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

all: l ld

l: l.c cache.h
	$(CC) $(CFLAGS) $(OMPFLAGS) -o $(BINDIR)/l l.c

ld: ld.c cache.h watch.h $(WATCH_SRC)
	$(CC) $(CFLAGS) -o $(BINDIR)/ld ld.c $(WATCH_SRC) $(WATCH_LIBS) -lpthread

clean:
	rm -f $(BINDIR)/l $(BINDIR)/ld

.PHONY: all clean
