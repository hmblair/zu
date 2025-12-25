CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -std=c99
OMPFLAGS ?= -fopenmp
BINDIR ?= ../../bin

l: l.c
	$(CC) $(CFLAGS) $(OMPFLAGS) -o $(BINDIR)/l $<

clean:
	rm -f $(BINDIR)/l

.PHONY: clean
