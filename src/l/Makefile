CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -std=c99 -Wno-unknown-warning-option -Wno-format-truncation -Wno-stringop-truncation
OMPFLAGS ?= -fopenmp
BINDIR ?= ../../bin

all: l l-cached link-l-cache

l: l.c cache.h dir_stats.h
	$(CC) $(CFLAGS) $(OMPFLAGS) -o $(BINDIR)/l l.c -lsqlite3

l-cached: ld.c cache.h
	$(CC) $(CFLAGS) -o $(BINDIR)/l-cached ld.c -lsqlite3

link-l-cache:
	chmod +x l-cache
	ln -sfn ../src/l/l-cache $(BINDIR)/l-cache

clean:
	rm -f $(BINDIR)/l $(BINDIR)/l-cached $(BINDIR)/l-cache

.PHONY: all clean link-l-cache
