CFLAGS += -Wall -std=gnu99

atc-ai: main.o pty.o vty.o
	$(CC) $(CFLAGS) -o $@ $^

main.o: main.c atc-ai.h

pty.o: pty.c atc-ai.h

vty.o: vty.c atc-ai.h

clean:
	rm atc-ai *.o
