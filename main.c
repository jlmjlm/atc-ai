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
#define DEF_DELAY_MS 500
#define DEF_TYPING_DELAY_MS 150
#define DEF_INTERVAL 50
#define DEF_IMIN 10
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
static int delay_ms = DEF_DELAY_MS;
static int typing_delay_ms = DEF_TYPING_DELAY_MS;
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
    write(ptm, "y", 1);
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
    write(sigpipe, &c, 1);
}

static void handle_abort(int signo) {
    static const char msg[] = "Caught abort signal.  Contents of the display:\n";
    write(logfd, msg, (sizeof msg)-1);
    write(logfd, display, screen_height*screen_width);
    write(logfd, "\n", 1);
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

#define CNTRL(x) (x-'A'+1)
static void handle_input(char c) {
    switch (c) {
        case CNTRL('C'):
            raise(SIGINT);
            return;
        case CNTRL('L'):
            write(ptm, &c, 1);
            break;
        case '+':
            delay_ms += interval;
            if (delay_ms > imax)
                delay_ms = imax;
            break;
        case '-':
            delay_ms -= interval;
            if (delay_ms < imin)
                delay_ms = imin;
            if (delay_ms == 0)
                write_queued_chars();
            break;
        default:
            write(1, "\a", 1);
            break;
    }
}

static inline void write_tqchar() {
     write(ptm, tqueue+tqhead, 1);
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

static inline const char *bstr(bool b) {
    return b ? "true" : "false";
}

static inline void check_update(bool *board_setup, struct timeval *deadline,
                                const struct timeval last_atc) {
    if (shutting_down)
        return;
    write_queued_chars();
    if (update_board(delay_ms <= mark_threshold)) {
        if (frame_no == duration_frame)
            shutdown_atc(SIGINT);
        else if (saved_planes >= duration_planes) {
            write_all_qchars();
            shutdown_atc(SIGINT);
        }
        *board_setup = true;
        *deadline = last_atc;
        deadline->tv_usec += 1000*delay_ms;
    }
    if (!delay_ms)
        write_queued_chars();
}

static noreturn void mainloop(int pfd) {
    bool board_setup = false;
    int maxfd = 0;
    fd_set fds;
    FD_ZERO(&fds);
    struct timeval deadline, last_atc;
    gettimeofday(&last_atc, NULL);
    deadline = last_atc;
    deadline.tv_usec += 1000*delay_ms;
    const time_t end_time = last_atc.tv_sec + duration_sec;

    for (;;) {
        struct timeval now;
        gettimeofday(&now, NULL);
        if (duration_sec && now.tv_sec > end_time) {
            shutdown_atc(SIGINT);
            duration_sec = 0;
        }
        int timeout_ms = (deadline.tv_sec - now.tv_sec)*1000 +
                         (deadline.tv_usec - now.tv_usec)/1000;
        if (timeout_ms <= 0)
            timeout_ms = 0;
        else if (tqhead != tqtail && typing_delay_ms != 0) {
            int qsize = tqtail-tqhead;
            if (qsize < 0)
                qsize += TQ_SIZE;
            timeout_ms /= qsize;

            if (typing_delay_ms && timeout_ms > typing_delay_ms)
                timeout_ms = typing_delay_ms;
        }

        struct timeval tv = { .tv_sec = timeout_ms / 1000,
                              .tv_usec = (timeout_ms % 1000) * 1000 };
        add_fd(0, &fds, &maxfd);
        add_fd(ptm, &fds, &maxfd);
        add_fd(pfd, &fds, &maxfd);
        struct timeval *ptv = (delay_ms && (board_setup || tqhead != tqtail))
                                  ? &tv : NULL;
        int rv = select(maxfd+1, &fds, NULL, NULL, ptv);

        if (rv == -1) {
            if (errno == EINTR) {
                errno = 0;
                continue;
            }
            errexit(errno, "select failed: %s", strerror(errno));
        }
        if (rv == 0) {   // timeout
            if (delay_ms)
                check_update(&board_setup, &deadline, last_atc);
            continue;
        }
        if (FD_ISSET(ptm, &fds)) {
            gettimeofday(&last_atc, NULL);
            process_data(ptm, BUFSIZE, &update_display);
            if (!board_setup || !delay_ms)
                check_update(&board_setup, &deadline, last_atc);
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
    { .name = NULL, .has_arg = 0, .flag = NULL, .val = '\0' }
};

static const char optstring[] = ":hd:t:sSTL:a:g:r:i:D:f:P:m:v";

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
    "            Increase the verbosity in the log file.\n";


static bool do_self_test = false;
static bool print_usage_message = false;
static intmax_t random_seed = -2;
static bool do_skip = false, dont_skip = false;
bool verbose = false;

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
                logfile_name = strdup(optarg);
                break;
            case 'a':
                atc_cmd = strdup(optarg);
                break;
            case 'g':
                game = strdup(optarg);
                break;
            case 'r':
                if (!strncmp(".", optarg, 2))
                    random_seed = -1;
                else
                    random_seed = atoll(optarg);
                break;
            case 'i':
                istr = strtok(optarg, ":");
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
                break;
            case 'D':
                istr = strtok(optarg, ":");
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
    logfd = fileno(logff);
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
        fprintf(logff, "Using RNG seed of %jd\n", random_seed);
    const char **args = make_args(argc - optind, argv + optind, random_seed);
    atc_pid = spawn(atc_cmd, args, ptm);
    free(args);

    raw_mode();
    mainloop(pipefd[0]);
    return 0;
}
