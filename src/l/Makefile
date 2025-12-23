CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -std=c99
BINDIR ?= ../../bin

l: l.c
	$(CC) $(CFLAGS) -o $(BINDIR)/l $<

clean:
	rm -f $(BINDIR)/l

.PHONY: clean
