#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <termios.h>
#include <signal.h>
#include <sys/select.h>

#include "atc-ai.h"

#define BUFSIZE 100

static int atc_pid;
static struct termios orig_termio;
static int sigpipe;
static int ptm;

static void restore_termio() {
    tcsetattr(1, TCSAFLUSH, &orig_termio);
}

static void raw_mode() {
    tcgetattr(1, &orig_termio);
    atexit(&restore_termio);
    struct termios new_termio = orig_termio;
    new_termio.c_lflag &= ~(ECHO | ICANON | IEXTEN);
    new_termio.c_iflag &= ~(ICRNL | ISTRIP | IXON);
    new_termio.c_oflag &= ~OPOST;
    new_termio.c_cc[VMIN] = 1;
    new_termio.c_cc[VTIME] = 0;
    tcsetattr(1, TCSAFLUSH, &new_termio);
}

static void terminate(int signo) {
    kill(atc_pid, signo);
    exit(0);
}

static void interrupt(int signo) {
    kill(atc_pid, signo);
    write(ptm, "y", 1);
}

static void handle_signal(int signo) {
    char c = signo;
    write(sigpipe, &c, 1);
}

static void reg_sighandler() {
    struct sigaction handler;
    handler.sa_handler = &handle_signal;
    sigemptyset(&handler.sa_mask);
    handler.sa_flags = 0;
    sigaction(SIGINT, &handler, NULL);
    sigaction(SIGTERM, &handler, NULL);
    sigaction(SIGWINCH, &handler, NULL);
    sigaction(SIGCLD, &handler, NULL);
}

static void add_fd(int fd, fd_set *fds, int *max) {
    if (fd > *max)
	*max = fd;
    FD_SET(fd, fds);
}

static void process_data(int src, int amt, void (*handler)(char)) {
    char buf[amt];
    int nchar;

    nchar = read(src, buf, amt);
    if (nchar == 0)
	exit(0);
    if (nchar == -1) {
        fprintf(stderr, "\nread failed: %s\n", strerror(errno));
        exit(errno);
    }
    for (int i = 0; i < nchar; i++)
	handler(buf[i]);
}

static void handle_input(char c) {
    if (c == 3) {
    	raise(SIGINT);
    	return;
    }
    write(ptm, &c, 1);
}

static void mainloop(int pfd) {
    int maxfd = 0;
    fd_set fds;
    FD_ZERO(&fds);

    for (;;) {
        add_fd(0, &fds, &maxfd);
        add_fd(ptm, &fds, &maxfd);
        add_fd(pfd, &fds, &maxfd);
	int rv = select(maxfd+1, &fds, NULL, NULL, NULL);
	if (rv == -1) {
	    if (errno == EINTR) {
		errno = 0;
		continue;
	    }
	    fprintf(stderr, "\nselect failed: %s\n", strerror(errno));
	    exit(errno);
	}
	if (FD_ISSET(ptm, &fds))
	    process_data(ptm, BUFSIZE, &update_display);
	if (FD_ISSET(0, &fds))
	    process_data(0, 1, &handle_input);
	if (FD_ISSET(pfd, &fds)) {
	    char signo;
	    read(pfd, &signo, 1);
	    switch(signo) {
		case SIGWINCH:
		    fprintf(stderr, "\nCan't handle window resize.\n");
		    // Fallthrough
		case SIGTERM:
		    terminate(signo);
		    // No return
		case SIGINT:
		    interrupt(signo);
		    break;
		case SIGCLD:
		    exit(0);
		default:
		    fprintf(stderr, "\nCaught unexpected signal %s\n",
			    strsignal(signo));
		    exit(signo);
	    }
	}
    }
}

int main(int argc, char **argv) {
    int pipefd[2];

    pipe(pipefd);
    sigpipe = pipefd[1];
    ptm = get_ptm();
    reg_sighandler();
    atc_pid = spawn("atc", argv, ptm);

    raw_mode();
    mainloop(pipefd[0]);
    return 0;
}
