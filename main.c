#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>
#include <stdarg.h>
#include <errno.h>
#include <termios.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/time.h>
#include <getopt.h>

#include "atc-ai.h"

#define BUFSIZE 1000
#define DEF_LOGFILE "atc-ai.log"
#define DEF_DELAY_MS 500


FILE *logff;
FILE *outf;

static int atc_pid;
static struct termios orig_termio;
static int sigpipe;
static int ptm;
static int delay_ms = DEF_DELAY_MS;
static int typing_delay_ms = 0;
static const char *logfile_name = DEF_LOGFILE;
static volatile sig_atomic_t cleanup_done = false;


void cleanup() {
    if (cleanup_done)
	return;

    cleanup_done = true;
    // Restore termio
    tcsetattr(1, TCSAFLUSH, &orig_termio);

    // Kill atc
    if (atc_pid)
        kill(atc_pid, SIGTERM);
}

__attribute__((__noreturn__, format(printf, 2, 3) ))
void errexit(int exit_code, const char *fmt, ...) {
    cleanup();
    putc('\n', stderr);
    
    va_list va;
    va_start(va, fmt);
    vfprintf(stderr, fmt, va);
    putc('\n', stderr);
    va_end(va);

    exit(exit_code);
}

static void shutdown_atc(int signo) {
    kill(atc_pid, signo);
    usleep(100000);  // .1 s
    write(ptm, "y", 1);
}

static void exit_hand() {
    shutdown_atc(SIGINT);
    atc_pid = 0;
    cleanup();
    usleep(100000);  // .1 s
}

static void raw_mode() {
    tcgetattr(1, &orig_termio);
    atexit(&exit_hand);
    struct termios new_termio = orig_termio;
    new_termio.c_lflag &= ~(ECHO | ICANON | IEXTEN);
    new_termio.c_iflag &= ~(ICRNL | ISTRIP | IXON);
    new_termio.c_oflag &= ~OPOST;
    new_termio.c_cc[VMIN] = 1;
    new_termio.c_cc[VTIME] = 0;
    tcsetattr(1, TCSAFLUSH, &new_termio);
}

noreturn static void terminate(int signo) {
    kill(atc_pid, signo);
    exit(0);
}

static void interrupt(int signo) {
    fprintf(logff, "Caught %s signal.  Contents of the display:\n%.*s\n",
	    strsignal(signo), screen_height*screen_width, display);
    shutdown_atc(signo);
}

noreturn static void abort_hand(int signo) {
    exit_hand();
    abort();
}

static void handle_signal(int signo) {
    char c = signo;
    write(sigpipe, &c, 1);
}

static void handle_abort(int signo) {
    cleanup();
    handle_signal(signo);
}

static void reg_sighandler() {
    struct sigaction handler;
    handler.sa_handler = &handle_signal;
    sigemptyset(&handler.sa_mask);
    handler.sa_flags = SA_RESETHAND;
    sigaction(SIGINT, &handler, NULL);
    sigaction(SIGTERM, &handler, NULL);
    sigaction(SIGWINCH, &handler, NULL);
    sigaction(SIGCLD, &handler, NULL);
    handler.sa_handler = &handle_abort;
    sigaction(SIGABRT, &handler, NULL);
}

static void add_fd(int fd, fd_set *fds, int *max) {
    if (fd > *max)
	*max = fd;
    FD_SET(fd, fds);
}

static void process_data(int src, int amt, void (*handler)(char)) {
    char buf[amt];
    int nchar;

    retry:
    nchar = read(src, buf, amt);
    if (nchar == 0)
	exit(0);
    if (nchar == -1) {
	if (errno == EINTR)
	    goto retry;
 	if (errno == EIO)
	    exit(0);
        errexit(errno, "read failed: %s", strerror(errno));
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

static void write_tqchar() {
     write(ptm, tqueue+tqhead, 1);
     tqhead = (tqhead+1)%TQ_SIZE;
}

static void write_queued_chars() {
    if (tqhead != tqtail) {
        if (typing_delay_ms)
	    write_tqchar();
	else {
	    while (tqhead != tqtail)
	        write_tqchar();
	}
    }
}

static void mainloop(int pfd) {
    int maxfd = 0;
    fd_set fds;
    FD_ZERO(&fds);
    struct timeval deadline, last_atc;
    gettimeofday(&last_atc, NULL);
    deadline = last_atc;
    deadline.tv_usec += 1000*delay_ms;

    for (;;) {
        struct timeval now;
	gettimeofday(&now, NULL);
	int timeout_ms = (deadline.tv_sec - now.tv_sec)*1000 +
			 (deadline.tv_usec - now.tv_usec)/1000;
	if (timeout_ms <= 0)
	    timeout_ms = 0;
	else if (tqhead != tqtail && typing_delay_ms != 0) {
	    int qsize = tqtail-tqhead;
	    if (qsize < 0)
		qsize += TQ_SIZE;
	    timeout_ms /= qsize;

	    if (timeout_ms > typing_delay_ms)
		timeout_ms = typing_delay_ms;
	}

        struct timeval tv = { .tv_sec = timeout_ms / 1000,
			      .tv_usec = (timeout_ms % 1000) * 1000 };
        //add_fd(0, &fds, &maxfd);
        add_fd(ptm, &fds, &maxfd);
        add_fd(pfd, &fds, &maxfd);
	struct timeval *ptv = delay_ms ? &tv : NULL;
	int rv = select(maxfd+1, &fds, NULL, NULL, ptv);
	struct timeval later;
        gettimeofday(&later, NULL);
	int elapsed_ms = (later.tv_sec - now.tv_sec)*1000 +
			 (later.tv_usec - now.tv_usec)/1000;

	if (elapsed_ms >= timeout_ms/2 && delay_ms) {
	    write_queued_chars();
	    if (update_board()) {
		deadline = last_atc;
	        deadline.tv_usec += 1000*delay_ms;
	    }
  	}

	if (rv == -1) {
	    if (errno == EINTR) {
		errno = 0;
		continue;
	    }
	    errexit(errno, "select failed: %s", strerror(errno));
	}
	if (rv == 0)
	    continue;    // timeout
	if (FD_ISSET(ptm, &fds)) {
	    gettimeofday(&last_atc, NULL);
	    process_data(ptm, BUFSIZE, &update_display);
	    if (!delay_ms && update_board())
		write_queued_chars();
	}
	if (FD_ISSET(0, &fds))
	    process_data(0, 1, &handle_input);
	if (FD_ISSET(pfd, &fds)) {
	    char signo;
	    read(pfd, &signo, 1);
	    switch(signo) {
		case SIGWINCH:
		    errexit('w', "Can't handle window resize.");
		    // No return
		case SIGTERM:
		    terminate(signo);
		    // No return
		case SIGABRT:
		    abort_hand(signo);
		    // No return
		case SIGINT:
		    interrupt(signo);
		    break;
		case SIGCLD:
		    exit(0);
		default:
		    errexit(signo, "Caught unexpected signal %s",
			    strsignal(signo));
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
	    case 't':
		typing_delay_ms = atoi(optarg);
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
	    case 'L':
	        logfile_name = strdup(optarg);
		break;
	    case 'r':
		if (!strncmp(".", optarg, 2))
		    random_seed = -1;
		else
		    random_seed = atoi(optarg);
		break;
	    //FIXME: Rest of the args
	    //FIXME: -g/--game (so don't have to "-- -g <game>")
	}
    }
}

static void write_cmd_args(int argc, char *const *argv) {
    fprintf(logff, "Command line args:");
    while (argc--) {
	fprintf(logff, " '%s'", *argv);
	argv++;
    }
    fputc('\n', logff);
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

    logff = fopen(logfile_name, "w");
    setvbuf(logff, NULL, _IOLBF, 0);
    write_cmd_args(argc, argv);

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
