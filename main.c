#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>
#include <termios.h>
#include <signal.h>
#include <sys/select.h>
#include <getopt.h>

#include "atc-ai.h"

#define BUFSIZE 100
#define LOGFILE "atc-ai.log"
#define DEF_DELAY_MS 500


FILE *logff;
FILE *outf;

static int atc_pid;
static struct termios orig_termio;
static int sigpipe;
static int ptm;
static int delay_ms = DEF_DELAY_MS;

static void cleanup() {
    // Restore termio
    tcsetattr(1, TCSAFLUSH, &orig_termio);

    // Kill atc
    if (atc_pid)
        kill(atc_pid, SIGTERM);
}

static void raw_mode() {
    tcgetattr(1, &orig_termio);
    atexit(&cleanup);
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
    fprintf(logff, "Caught interrupt signal.  Contents of the display:\n%.*s\n",
	    screen_height*screen_width, display);
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
        struct timeval tv = { .tv_sec = delay_ms / 1000,
			      .tv_usec = (delay_ms % 1000) * 1000 };
        //add_fd(0, &fds, &maxfd);
        add_fd(ptm, &fds, &maxfd);
        add_fd(pfd, &fds, &maxfd);
	int rv = select(maxfd+1, &fds, NULL, NULL, &tv);
	if (rv == -1) {
	    if (errno == EINTR) {
		errno = 0;
		continue;
	    }
	    fprintf(stderr, "\nselect failed: %s\n", strerror(errno));
	    exit(errno);
	}
	if (rv == 0) {	// timeout
	    update_board();
	    continue;
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

static char **make_args(int argc, char **argv, int seed) {
    if (*argv && !strncmp("--", *argv, 3)) {
	argc--;
	argv++;
    }

    char **args = malloc((argc+4)*sizeof(*args));
    int i = 0;

    args[i++] = "atc";

    if (seed != -1) {
	static char buf[30];
	sprintf(buf, "%d", seed);
	args[i++] = "-r";
	args[i++] = buf;
    }

    for (;;) {
	if (!(args[i++] = *(argv++)))
	    return args;
    }
}

static const struct option ai_opts[] = {
    { .name = "seed", .has_arg = required_argument, .flag = NULL, .val = 'r' },
    { .name = "delay", .has_arg = required_argument, .flag = NULL, .val = 'd' },
    { .name = "skip", .has_arg = no_argument, .flag = NULL, .val = 's' },
    { .name = "dont-skip", .has_arg = no_argument, .flag = NULL, .val = 'S' },
    { .name = "self-test", .has_arg = no_argument, .flag = NULL, .val = 'T' },
    { .name = "logfile", .has_arg = required_argument, .flag = NULL,
	  .val = 'L' },
    { .name = "frames", .has_arg = required_argument, .flag = NULL, .val = 'f'},
    { .name = "saved-planes", .has_arg = required_argument, .flag = NULL,
	  .val = 'p' },
    { .name = "time", .has_arg = required_argument, .flag = NULL, .val = 'D' },
    { .name = "typing-delay", .has_arg = required_argument, .flag = NULL,
	  .val = 't' },
    { .name = "help", .has_arg = no_argument, .flag = NULL, .val = 'h' },
    { .name = "atc-cmd", .has_arg = required_argument, .flag = NULL,
          .val = 'a' },
    { .name = "interval", .has_arg = required_argument, .flag = NULL,
          .val = 'i' },
    { .name = NULL, .has_arg = 0, .flag = NULL, .val = '\0' }
};

static const char optstring[] = ":r:d:sSTL:f:p:D:t:ha:i:";

static const char usage[] =
    "Usage:  atc-ai <ai-args> [-- <atc-args>]\n"
    "    AI args:\n"
    "        -r|--seed <seed>\n"
    "            Random seed for 'atc' (default is epoch time).\n"
    "            Use '.' to not pass a seed to 'atc'.\n"
    "        -d|--delay <ms>\n"
    "            Milliseconds to wait after an 'atc' write before moving.\n"
    "            (default 500)\n"
    "        -s|--skip\n"
    "            After moving, skip to the next tick.  (default)\n"
    "        -S|--dont-skip\n"
    "            After moving, wait for 'atc' to advance.\n"
    "        -T|--self-test\n"
    "            Run a self-test.\n"
    "        -L|--logfile <filename>\n"
    "            Log to write to.  (default \"atc-ai.log\")\n"
    "        -f|--frames <frame number>\n"
    "            Exit after this many frames.\n"
    "        -p|--saved-planes <number of planes>\n"
    "            Exit after \"saving\" this many planes.\n"
    "        -D|--time <number of seconds>\n"
    "            Exit after a duration of this many seconds.\n"
    "        -t|--typing-delay <ms>\n"
    "            Duration to pause between commands to 'atc' in milliseconds.\n"
    "            (default 0)\n"
    "        -h|--help\n"
    "            Display this usage message instead of running.\n"
    "        -a|--atc-cmd <command>\n"
    "            Command to run.  (default \"atc\")\n"
    "        -i|--interval <ms>[:<ms>:<ms>]\n"
    "            Interval to change frame delay on a '+'/'-' keypress,\n"
    "            and maximum and minimum values in milliseconds.\n"
    "            (defaults: 100, 900, 100)\n";


static bool do_self_test = false;
static bool print_usage_message = false;
static int random_seed = -2;
static bool do_skip = false, dont_skip = false;

static void process_cmd_args(int argc, char *const argv[]) {
    int arg;
    for (;;) {
        arg = getopt_long(argc, argv, optstring, ai_opts, NULL);
	switch (arg) {
	    case -1: 
		return;
	    case ':': case '?': case 'h':
	        print_usage_message = true;
		return;
	    case 'd':
		delay_ms = atoi(optarg);
		break;
	    case 's':
		do_skip = true;
		break;
	    case 'S':
		dont_skip = true;
		break;
	    case 'T':
		do_self_test = true;
		break;
	    case 'r':
		if (!strncmp(".", optarg, 2))
		    random_seed = -1;
		else
		    random_seed = atoi(optarg);
		break;
	    //XXX: Rest of the args
	}
    }
}

int main(int argc, char **argv) {
    int pipefd[2];

    process_cmd_args(argc, argv);

    if (do_skip && dont_skip) {
 	fprintf(stderr, "Both 'do' and 'dont' skip requested.\n");
	print_usage_message = true;
    } else {
	skip_tick = !dont_skip;
    }

    if (print_usage_message) {
	fputs(usage, stdout);
	return 1;
    }

    if (delay_ms == 0) {
	printf("Bad delay amount.\n");
	return 2;
    }

    logff = fopen(LOGFILE, "w");
    setvbuf(logff, NULL, _IOLBF, 0);

    if (do_self_test) {
	return testmain();
    }

    pipe(pipefd);
    sigpipe = pipefd[1];
    ptm = get_ptm();
    reg_sighandler();
    if (random_seed == -2)
	random_seed = time(NULL);
    if (random_seed == -1)
	fprintf(logff, "Using no random seed.\n");
    else
        fprintf(logff, "Using RNG seed of %d\n", random_seed);
    char **args = make_args(argc - optind, argv + optind, random_seed);
    atc_pid = spawn("atc", args, ptm);
    free(args);

    raw_mode();
    outf = fdopen(ptm, "w");
    setvbuf(outf, NULL, _IOLBF, 0);
    mainloop(pipefd[0]);
    return 0;
}
