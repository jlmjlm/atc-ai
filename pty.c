// Copyright 2013 Jacob L. Mandelson
// This code may be distributed under the terms of the Affero General
// Public License, ver. 3.  See the file "AGPLv3" for details.

#define _XOPEN_SOURCE 700

#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>

#include "atc-ai.h"

#define PTMX "/dev/ptmx"


int get_ptm() {
    struct winsize ws;
    int ptm = open(PTMX, O_RDWR|O_NOCTTY);
    grantpt(ptm);
    unlockpt(ptm);
    ioctl(0, TIOCGWINSZ, &ws);
    ioctl(ptm, TIOCSWINSZ, &ws);
    screen_height = ws.ws_row;
    screen_width = ws.ws_col;
    display = malloc(screen_height*screen_width+1);
    memset(display, ' ', screen_height*screen_width);
    display[screen_height*screen_width] = '\0';
    return ptm;
}

int spawn(const char *cmd, const char *args[], int ptm) {
    fflush(stderr);
    int pid = fork();
    if (pid == 0) {
        setenv("TERM", "vt100", 1);
        unsetenv("TERMCAP");
        const char *ptsfn = ptsname(ptm);
        close(ptm);  close(0);  close(1);  close(2);
        int dt = open("/dev/tty", O_RDWR);
        if (dt != -1)
            ioctl(dt, TIOCNOTTY);
        int pts = open(ptsfn, O_RDWR);
        setsid();
        ioctl(pts, TIOCSCTTY);
        dup2(pts, 0);
        dup2(pts, 1);
        dup2(pts, 2);
        args[0] = cmd;
        execvp(cmd, (char *const *)args);   // See SUS's rationale on the exec*
                                            // functions to explain the cast.
        fprintf(stderr, "exec of %s failed: %s\n", cmd, strerror(errno));
        fflush(stderr);
        _exit(-1);
    }
    return pid;
}
