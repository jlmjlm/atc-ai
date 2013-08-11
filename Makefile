CFLAGS += -Wall

atc-ai: main.o pty.o
	$(CC) $(CFLAGS) -o $@ $^

main.o: main.c atc-ai.h

pty.o: pty.c atc-ai.h

vty.c: vty.o atc-ai.h
