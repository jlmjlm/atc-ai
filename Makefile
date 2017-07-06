# Edit install dir as you please
INSTALLDIR = /usr/local/bin

####

CFLAGS += -Wall -std=gnu99 -O

all: test atc-ai

.PHONY: clean install uninstall all test wslint check

atc-ai: main.o pty.o vty.o board.o orders.o pathfind.o testpath.o
	$(CC) $(CFLAGS) -o $@ $^

test: atc-ai
	./atc-ai --self-test -L self-test.log

check: test wslint

main.o: main.c atc-ai.h

pty.o: pty.c atc-ai.h

vty.o: vty.c atc-ai.h

board.o: board.c atc-ai.h pathfind.h

pathfind.o: pathfind.c atc-ai.h pathfind.h

orders.o: orders.c atc-ai.h

testpath.o: testpath.c atc-ai.h pathfind.h

clean:
	-rm atc-ai *.o

install: atc-ai
	cp atc-ai ${INSTALLDIR}

uninstall:
	rm ${INSTALLDIR}/atc-ai

wslint:
	! grep -l ' $$' `cat MANIFEST`
	! grep -l -F --exclude 'Makefile*' $$'\t' `cat MANIFEST`
	@echo PASS
