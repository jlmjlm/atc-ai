# Edit install dir as you please
INSTALLDIR = /usr/local/bin

####

CFLAGS += -Wall -std=gnu99

atc-ai: main.o pty.o vty.o board.o orders.o pathfind.o
	$(CC) $(CFLAGS) -o $@ $^

main.o: main.c atc-ai.h

pty.o: pty.c atc-ai.h

vty.o: vty.c atc-ai.h

board.o: board.c atc-ai.h

pathfind.o: pathfind.c atc-ai.h

orders.o: orders.c atc-ai.h

clean:
	rm atc-ai *.o

install: atc-ai
	cp atc-ai ${INSTALLDIR}

uninstall:
	rm ${INSTALLDIR}/atc-ai
