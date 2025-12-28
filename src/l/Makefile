CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -std=c99 -Wno-unknown-warning-option -Wno-format-truncation -Wno-stringop-truncation
OMPFLAGS ?= -fopenmp
BINDIR ?= ../../bin

# Detect libgit2
HAVE_LIBGIT2 := $(shell pkg-config --exists libgit2 2>/dev/null && echo 1)
ifeq ($(HAVE_LIBGIT2),1)
  LIBGIT2_CFLAGS := $(shell pkg-config --cflags libgit2) -DHAVE_LIBGIT2
  LIBGIT2_LIBS := $(shell pkg-config --libs libgit2)
endif

all: l l-cached link-l-cache

l: l.c cache.h dir_stats.h
	$(CC) $(CFLAGS) $(OMPFLAGS) $(LIBGIT2_CFLAGS) -o $(BINDIR)/l l.c -lsqlite3 $(LIBGIT2_LIBS)

l-cached: ld.c cache.h
	$(CC) $(CFLAGS) -o $(BINDIR)/l-cached ld.c -lsqlite3

link-l-cache:
	chmod +x l-cache
	ln -sfn ../src/l/l-cache $(BINDIR)/l-cache

clean:
	rm -f $(BINDIR)/l $(BINDIR)/l-cached $(BINDIR)/l-cache

.PHONY: all clean link-l-cache
