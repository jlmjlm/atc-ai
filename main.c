// Copyright 2013 Jacob L. Mandelson
// This code may be distributed under the terms of the Affero General
// Public License, ver. 3.  See the file "AGPLv3" for details.

#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>
#include <stdarg.h>
#include <errno.h>
#include <termios.h>
#include <signal.h>
#include <limits.h>
#include <inttypes.h>
#include <sys/select.h>
#include <sys/time.h>
#include <getopt.h>

#include "atc-ai.h"

#define BUFSIZE 1000
#define DEF_LOGFILE "atc-ai.log"
#define DEF_DELAY_MS 400
#define DEF_TYPING_DELAY_MS 150
#define DEF_INTERVAL 50
#define DEF_IMIN 0
#define DEF_IMAX 900
#define DEF_MARK_THRESHOLD 50
#define STR(x) XSTR(x)
#define XSTR(x) #x
#undef CTRL


FILE *logff;
char erase_char;
static volatile sig_atomic_t logfd;    // For use in the SIGABRT handler.

static volatile sig_atomic_t atc_pid = 0;
static const char *atc_cmd = "atc";
static const char *game = NULL;
static struct termios orig_termio;
static int sigpipe;
static int ptm;
static unsigned int delay_ms = DEF_DELAY_MS;
static unsigned int typing_delay_ms = DEF_TYPING_DELAY_MS;
static const char *logfile_name = DEF_LOGFILE;
static volatile sig_atomic_t cleanup_done = false;
static bool shutting_down = false;
static int interval = DEF_INTERVAL, imin = DEF_IMIN, imax = DEF_IMAX;
static int duration_sec = 0;
static int duration_frame = -10;
static int duration_planes = INT_MAX;
static int mark_threshold = DEF_MARK_THRESHOLD;

static void write_queued_chars(void);
static inline void write_all_qchars(void);


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
    fprintf(logff, "Contents of the display:\n%.*s\n",
            screen_height*screen_width, display);
    cleanup();
    putc('\n', stderr);

    va_list va;
    va_start(va, fmt);
    vfprintf(stderr, fmt, va);
    putc('\n', stderr);
    va_end(va);

    exit(exit_code);
}

// Avoid compiler warning re unused return value from write()
void vwrite(int fd, const char *data, int nbytes) {
    int v = write(fd, data, nbytes); v = v;
}

static inline void msleep(int ms) {
    usleep(ms * 1000);
}

static void shutdown_atc(int signo) {
    if (shutting_down)
        return;
    shutting_down = true;
    msleep(100);  // .1 s
    kill(atc_pid, signo);
    msleep(100);  // .1 s
    vwrite(ptm, "y", 1);
}

static void exit_hand() {
    shutdown_atc(SIGINT);
    atc_pid = 0;
    cleanup();
    msleep(100);  // .1 s
}

static void interrupt(int signo) {
    fprintf(logff, "Caught %s signal.  Contents of the display:\n%.*s\n",
            strsignal(signo), screen_height*screen_width, display);
    shutdown_atc(signo);
}

noreturn static void terminate(int signo) {
    kill(atc_pid, signo);
    exit(0);
}

noreturn static void abort_hand(int signo) {
    fprintf(logff, "Handling abort (signo == %d [%s]) in eventloop "
                   "handler!  Danger!  Contents of the display:\n%.*s\n",
            signo, strsignal(signo), screen_height*screen_width, display);
    exit_hand();
    abort();
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
    erase_char = orig_termio.c_cc[VERASE];
}

static void handle_signal(int signo) {
    char c = signo;
    vwrite(sigpipe, &c, 1);
}

static void handle_abort(int signo) {
    static const char msg[] = "Caught abort signal.  Contents of the display:\n";
    vwrite(logfd, msg, (sizeof msg)-1);
    vwrite(logfd, display, screen_height*screen_width);
    vwrite(logfd, "\n", 1);
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

static void process_data(int src, int amt, void (*handler)(const char *, int)) {
    char buf[amt];
    int nchar;

    retry:
    nchar = read(src, buf, amt);
    if (nchar == 0)
        exit(0);
    if (nchar == -1) {
        if (errno == EINTR) {
            errno = 0;
            goto retry;
        }
        if (errno == EIO)
            exit(0);
        errexit(errno, "read failed: %s", strerror(errno));
    }
    handler(buf, nchar);
}

static inline void newdelay(int nd) {
    if (nd > imax)
        delay_ms = imax;
    else if (nd < imin)
        delay_ms = imin;
    else
        delay_ms = nd;

    if (!nd)
        write_queued_chars();

    if (verbose)
        fprintf(logff, "Setting frame delay to %d.\n", delay_ms);
}

#define CNTRL(x) (x-'A'+1)
static void handle_input_char(char c) {
    switch (c) {
        case CNTRL('C'):
            raise(SIGINT);
            return;
        case CNTRL('L'):
            vwrite(ptm, &c, 1);
            break;
        case '+':
            newdelay(delay_ms + interval);
            break;
        case '-':
            newdelay(delay_ms - interval);
            break;
        case '*':
            newdelay(delay_ms*2);
            break;
        case '/':
            newdelay(delay_ms/2);
            break;
        default:
            vwrite(1, "\a", 1);
            break;
    }
}

static void handle_input(const char *buf, int nchar) {
    for (int i = 0; i < nchar; i++)
        handle_input_char(buf[i]);
}

static inline void write_tqchar() {
     vwrite(ptm, tqueue+tqhead, 1);
     tqhead = (tqhead+1)%TQ_SIZE;
}

static inline void write_all_qchars() {
    while (tqhead != tqtail)
        write_tqchar();
}

static void write_queued_chars() {
    if (tqhead != tqtail) {
        if (typing_delay_ms && delay_ms)
            write_tqchar();
        else
            write_all_qchars();
    }
}

static inline void set_timeval_from_ms(struct timeval *tv, unsigned int ms) {
    tv->tv_sec = ms / 1000;
    tv->tv_usec = (ms % 1000) * 1000;
}

static void check_update(struct timeval *deadline, struct timeval *last_atc) {
    if (shutting_down)
        return;
    if (update_board(delay_ms <= mark_threshold)) {
        if (frame_no == duration_frame)
            shutdown_atc(SIGINT);
        else if (saved_planes >= duration_planes) {
            write_all_qchars();
            shutdown_atc(SIGINT);
        }
        gettimeofday(last_atc, NULL);
        *deadline = *last_atc;
        deadline->tv_usec += 1000*delay_ms;  //FIXME: Should we really be doing
                                             //       this if delay_ms==0 ?
    }
    if (!delay_ms || !typing_delay_ms)
        write_all_qchars();
}

static noreturn void mainloop(int pfd) {
    /* 'last_atc' is the time at which we last processed the board and
     * noticed that 'atc' had updated it with a new frame.
     * 'deadline' is the time before which we should have all our orders
     * "typed" into atc's terminal, and is when we will check the display
     * for an update from 'atc'.  If deadline.tv_sec == 0, we are pended on
     * data coming from 'atc', so we wait in the select() indefinitely, and
     * upon getting data from atc's terminal, we set deadline to now+delay_ms.
     */
    struct timeval deadline = { .tv_sec = 0, .tv_usec = 0 }, last_atc;
    gettimeofday(&last_atc, NULL);

    const time_t end_time = last_atc.tv_sec + duration_sec;
    mark_msg();
    write_all_qchars();

    int maxfd = 0;
    fd_set fds;
    FD_ZERO(&fds);

    for (;;) {
        struct timeval now, waittv, *ptv;
        gettimeofday(&now, NULL);
        if (duration_sec && now.tv_sec > end_time) {
            shutdown_atc(SIGINT);
            duration_sec = 0;
        }

        if (deadline.tv_sec == 0) {
            if (tqhead == tqtail)
                ptv = NULL;
            else {
                set_timeval_from_ms(&waittv, typing_delay_ms);
                ptv = &waittv;
            }
        } else {
            int timeout_ms = (deadline.tv_sec - now.tv_sec)*1000 +
                             (deadline.tv_usec - now.tv_usec)/1000;
            if (timeout_ms < 0)
                timeout_ms = 0;
            else if (tqhead != tqtail) {
                unsigned int qsize = (tqtail-tqhead)%TQ_SIZE;
                timeout_ms /= qsize;

                if (timeout_ms > typing_delay_ms)
                    timeout_ms = typing_delay_ms;
            }

            set_timeval_from_ms(&waittv, timeout_ms);
            ptv = &waittv;
        }

        add_fd(0, &fds, &maxfd);
        add_fd(ptm, &fds, &maxfd);
        add_fd(pfd, &fds, &maxfd);
        int rv = select(maxfd+1, &fds, NULL, NULL, ptv);

        if (rv == -1) {
            if (errno == EINTR) {
                errno = 0;
                continue;
            }
            errexit(errno, "select failed: %s", strerror(errno));
        }
        if (rv == 0) {   // timeout
            if (tqhead != tqtail) {
                write_queued_chars();
            } else if (deadline.tv_sec == 0) {
                fprintf(logff, "Danger: timeout when pended and no chars "
                               "to type from the queue.\n");
            } else {
                deadline.tv_sec = 0;
                check_update(&deadline, &last_atc);
            }

            continue;
        }
        if (FD_ISSET(ptm, &fds)) {
            process_data(ptm, BUFSIZE, &update_display);
            if (delay_ms && deadline.tv_sec == 0) {
                gettimeofday(&deadline, NULL);
                deadline.tv_usec += delay_ms * 1000;
            }
            if (frame_no == 0 || !delay_ms)
                check_update(&deadline, &last_atc);
        }
        if (FD_ISSET(0, &fds)) {
            process_data(0, BUFSIZE, &handle_input);
            if (!delay_ms)
                check_update(&deadline, &last_atc);
        }
        if (FD_ISSET(pfd, &fds)) {
            char signo;
            int v = read(pfd, &signo, 1); v=v;
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
                    // No return
                default:
                    errexit(signo, "Caught unexpected signal %s",
                            strsignal(signo));
                    // No return
            }
        }
    }
}

static const char **make_args(int argc, char **argv, intmax_t seed) {
    if (*argv && !strncmp("--", *argv, 3)) {
        argc--;
        argv++;
    }

    const char **args = malloc((argc+6)*sizeof(*args));
    int i = 0;

    args[i++] = atc_cmd;

    if (seed != -1) {
        static char buf[30];
        sprintf(buf, "%jd", seed);
        args[i++] = "-r";
        args[i++] = buf;
    }

    if (game) {
        args[i++] = "-g";
        args[i++] = game;
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
    { .name = "game", .has_arg = required_argument, .flag = NULL, .val = 'g' },
    { .name = "interval", .has_arg = required_argument, .flag = NULL,
          .val = 'i' },
    { .name = "mark", .has_arg = required_argument, .flag = NULL, .val = 'm' },
    { .name = "verbose", .has_arg = no_argument, .flag = NULL, .val = 'v' },
    { .name = "quiet", .has_arg = no_argument, .flag = NULL, .val = 'q' },
    { .name = NULL, .has_arg = 0, .flag = NULL, .val = '\0' }
};

static const char optstring[] = ":hd:t:sSTL:a:g:r:i:D:f:P:m:vq";

static const char usage[] =
    "Usage:  atc-ai [<ai-args>] [-- <atc-args>]\n"
    "    AI args:\n"
    "        -r|--seed <seed>\n"
    "            Random seed for 'atc' (default is epoch time).\n"
    "            Use '.' to not pass a seed to 'atc'.\n"
    "        -d|--delay <ms>\n"
    "            Milliseconds to wait after an 'atc' write before moving.\n"
    "            (default " STR(DEF_DELAY_MS) ")\n"
    "        -g|--game <name>\n"
    "            Name of game board to use.  (no default)\n"
    "        -s|--skip\n"
    "            After moving, skip to the next tick.  (default)\n"
    "        -S|--dont-skip\n"
    "            After moving, wait for 'atc' to advance.\n"
    "        -T|--self-test\n"
    "            Run a self-test.\n"
    "        -L|--logfile <filename>\n"
    "            Log to write to.  (default \"" DEF_LOGFILE "\")\n"
    "        -f|--frames <frame number>\n"
    "            Exit after this many frames.\n"
    "        -p|--saved-planes <number of planes>\n"
    "            Exit after \"saving\" this many planes.\n"
    "        -D|--time [[hh:]mm:]ss\n"
    "            Exit after a duration of this many hours:minutes:seconds.\n"
    "        -t|--typing-delay <ms>\n"
    "            Duration to pause between commands to 'atc' in milliseconds.\n"
    "            (default " STR(DEF_TYPING_DELAY_MS) ")\n"
    "        -h|--help\n"
    "            Display this usage message instead of running.\n"
    "        -a|--atc-cmd <command>\n"
    "            Command to run.  (default \"atc\")\n"
    "        -i|--interval <ms>[:<ms>:<ms>]\n"
    "            Interval to change frame delay on a '+'/'-' keypress,\n"
    "            and maximum and minimum values in milliseconds.\n"
    "            " STR((defaults: DEF_INTERVAL, DEF_IMAX, DEF_IMIN)) "\n"
    "        -m|--mark <ms>\n"
    "            Apply mark/unmark synchronization when the move delay\n"
    "            is this value or smaller.  (default "
                 STR(DEF_MARK_THRESHOLD) ")\n"
    "        -v|--verbose\n"
    "            Increase the verbosity in the log file.\n"
    "        -q|--quiet\n"
    "            Decrease the verbosity in the log file.\n"
    "\n"
    "atc-ai was written by Jacob L. Mandelson, and may be distributed in\n"
    "accordance with the Affero General Public License v. 3.\n";


static bool do_self_test = false;
static bool print_usage_message = false;
static intmax_t random_seed = -2;
static bool do_skip = false, dont_skip = false;
bool verbose = false, quiet = false;

static void process_cmd_args(int argc, char *const argv[]) {
    int arg;
    for (;;) {
        arg = getopt_long(argc, argv, optstring, ai_opts, NULL);
        switch (arg) {
            const char *istr;
            case -1:
                return;
            case ':': case '?': case 'h': default:
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
                logfile_name = optarg;
                break;
            case 'a':
                atc_cmd = optarg;
                break;
            case 'g':
                game = optarg;
                break;
            case 'r':
                if (!strncmp(".", optarg, 2))
                    random_seed = -1;
                else
                    random_seed = atoll(optarg);
                break;
            case 'i':
                istr = strtok(strdup(optarg), ":");
                if (!istr)
                    errexit('i', "strtok of \"%s\" failed", optarg);
                interval = atoi(istr);
                istr = strtok(NULL, ":");
                if (istr) {
                    imax = atoi(istr);
                    istr = strtok(NULL, ":");
                    if (istr)
                        imin = atoi(istr);
                    else
                        print_usage_message = true;
                }
                if (imin > imax) {
                    int t = imin;
                    imin = imax;
                    imax = t;
                }
                break;
            case 'D':
                istr = strtok(strdup(optarg), ":");
                while (istr) {
                    duration_sec = 60*duration_sec + atoi(istr);
                    istr = strtok(NULL, ":");
                }
                break;
            case 'f':
                duration_frame = atoi(optarg);
                if (!duration_frame)
                    print_usage_message = true;
                break;
            case 'p':
                duration_planes = atoi(optarg);
                break;
            case 'm':
                mark_threshold = atoi(optarg);
                break;
            case 'v':
                verbose = true;
                break;
            case 'q':
                quiet = true;
                break;
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

    if (verbose && quiet) {
        fprintf(stderr, "Both 'verbose' and 'quiet' requested.\n");
        print_usage_message = true;
    }

    if (print_usage_message) {
        fputs(usage, stdout);
        return 1;
    }

    logff = fopen(logfile_name, "w");
    setvbuf(logff, NULL, _IOLBF, 0);
    logfd = fileno(logff);
    write_cmd_args(argc, argv);

    if (do_self_test) {
        return testmain();
    }

    int v = pipe(pipefd); v=v;
    sigpipe = pipefd[1];
    ptm = get_ptm();
    reg_sighandler();
    if (random_seed == -2)
        random_seed = time(NULL);
    if (random_seed == -1)
        fprintf(logff, "Using no random seed.\n");
    else
        fprintf(logff, "Using RNG seed of %jd\n", random_seed);
    const char **args = make_args(argc - optind, argv + optind, random_seed);
    atc_pid = spawn(atc_cmd, args, ptm);
    free(args);

    raw_mode();
    mainloop(pipefd[0]);
    return 0;
}
