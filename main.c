#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <termios.h>
#include <signal.h>

#include "atc-ai.h"

#define BUFSIZE 100

int atc_pid;
static struct termios orig_termio;

static void raw_mode() {
    tcgetattr(1, &orig_termio);
    struct termios new_termio = orig_termio;
    new_termio.c_lflag &= ~(ECHO | ICANON | IEXTEN);
    new_termio.c_iflag &= ~(ICRNL | ISTRIP | IXON);
    new_termio.c_oflag &= ~OPOST;
    new_termio.c_cc[VMIN] = 1;
    new_termio.c_cc[VTIME] = 0;
    tcsetattr(1, TCSAFLUSH, &new_termio);
}

static void terminate(int signo) {
    tcsetattr(1, TCSAFLUSH, &orig_termio);
    kill(atc_pid, signo);
    exit(0);
}

static void reg_sighandler() {
    struct sigaction handler;
    handler.sa_handler = &terminate; 
    sigemptyset(&handler.sa_mask);
    handler.sa_flags = 0;
    sigaction(SIGINT, &handler, NULL);
    sigaction(SIGTERM, &handler, NULL);
}

int main(int argc, char **argv) {
    char buf[BUFSIZE];
    reg_sighandler();
    int ptm = get_ptm();
    atc_pid = spawn("atc", argv, ptm);

    raw_mode();
    int nchar;
    while ((nchar = read(ptm, buf, BUFSIZE)) > 0) {
	write(1, buf, nchar);
    }
    if (nchar == -1) {
	fprintf(stderr, "\nread failed: %s\n", strerror(errno));
	exit(errno);
    }
    return 0;
}
