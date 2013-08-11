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

static void terminate_sighand(int signo) {
    char c = signo;
    write(sigpipe, &c, 1);
}

static void reg_sighandler() {
    struct sigaction handler;
    handler.sa_handler = &terminate_sighand;
    sigemptyset(&handler.sa_mask);
    handler.sa_flags = 0;
    sigaction(SIGINT, &handler, NULL);
    sigaction(SIGTERM, &handler, NULL);
}

static void add_fd(int fd, fd_set *fds, int *max) {
    if (fd > *max)
	*max = fd;
    FD_SET(fd, fds);
}

static void xfer_data(int src, int dst, int amt) {
    char buf[amt];
    int nchar;
	    
    nchar = read(src, buf, amt);
    if (nchar == 0)
	exit(0);
    if (nchar == -1) {
        fprintf(stderr, "\nread failed: %s\n", strerror(errno));
        exit(errno);
    }
    write(dst, buf, nchar);
}

static void mainloop(int ptm, int pfd) {
    int maxfd = 0;
    fd_set fds;
    FD_ZERO(&fds);

    for (;;) {
        add_fd(0, &fds, &maxfd);
        add_fd(ptm, &fds, &maxfd);
        add_fd(pfd, &fds, &maxfd);
	select(maxfd+1, &fds, NULL, NULL, NULL);
	if (FD_ISSET(ptm, &fds))
	    xfer_data(ptm, 1, BUFSIZE);
	if (FD_ISSET(0, &fds))
	    xfer_data(0, ptm, 1);
	if (FD_ISSET(pfd, &fds)) {
	    char signo;
	    read(pfd, &signo, 1);
	    terminate(signo);
	}
    }
}
	    

int main(int argc, char **argv) {
    int pipefd[2];

    pipe(pipefd);
    sigpipe = pipefd[1];
    reg_sighandler();
    int ptm = get_ptm();
    atc_pid = spawn("atc", argv, ptm);

    raw_mode();
    mainloop(ptm, pipefd[0]);
    return 0;
}
