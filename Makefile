CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra
iqfan: iqfan.c
	$(CC) $(CFLAGS) -o $@ $<
clean:
	rm -f iqfan
.PHONY: clean
