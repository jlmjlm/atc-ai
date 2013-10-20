#include <stdbool.h>
#include <stdio.h>

extern int get_ptm(void);
extern int spawn(char *cmd, char *args[], int ptm);
extern void update_display(char);
extern bool update_board(void);
extern void cleanup(void);
extern int testmain(void);

__attribute__((noreturn, format(printf, 2, 3) ))
void errexit(int exit_code, const char *fmt, ...);


extern int screen_height, screen_width;
extern char *display;

extern FILE *logff;
extern FILE *outf;

#define D(row, col) (display[(row)*screen_width + (col)])
#define EXIT_MAX 10
#define AIRPORT_MAX 10

struct xy { int row, col; };
struct xyz { int row, col, alt; };
struct exitspec {
    int num;
    int row, col;
};
extern struct exitspec exits[EXIT_MAX];
extern int n_exits;
extern struct exitspec *get_exit(int);

extern int board_width, board_height, info_col;


struct bearing {
    int degree;
    int drow, dcol;
    char key;
    char aircode;
    const char *shortname;
    const char *longname;
};
extern const struct bearing *const bearings;
extern int bearing_of(const char *s);

static inline struct xy apply(int row, int col, int bearing) {
    row += bearings[bearing].drow;
    col += bearings[bearing].dcol;
    struct xy rv = { row, col };
    return rv;
}


#define EZ_SIZE 6
struct airport {
    int num;
    int bearing;
    int row, col;
    int trow, tcol;   // target
    int strow1, stcol1, strow2, stcol2;	// secondary targets
    struct xy exc[EZ_SIZE]; // exclusion zone
};
extern struct airport airports[AIRPORT_MAX];
extern int n_airports;
extern struct airport *get_airport(int n);

struct course {
    struct xyz pos;
    int bearing;
    bool cleared_exit, at_exit;
    struct course *prev, *next;
};

extern struct course *free_course_entry(struct course *);

struct plane {
    char id;
    bool isjet;
    bool target_airport;
    int target_num;
    struct course *start, *current, *end;
    int start_tm, current_tm, end_tm;
    struct plane *prev, *next;
};

extern void plot_course(struct plane *, int row, int col, int alt);

// The board's dynamic state.
extern int frame_no;
extern struct plane *plstart, *plend;


#define TQ_SIZE 1024
extern bool skip_tick;
extern int tqhead, tqtail;
extern char tqueue[TQ_SIZE];

extern void order_new_bearing(char id, int bearing);
extern void order_new_altitude(char id, int alt);
extern void land_at_airport(char id, int airport_num);
extern void next_tick(void);


// So it can be called from testpath.c
extern struct plane *remove_plane(struct plane *p);
extern void remove_course_entries(struct course *c);


// Cheapo check for mem leaks
extern int n_malloc, n_free;
extern void *count_malloc(const char *, size_t);
extern void count_free(const char *, void *);
#ifndef malloc
    #define malloc(x) count_malloc(__func__, x)
    #define free(x) count_free(__func__, x)
#endif


#define noreturn __attribute__((noreturn))
