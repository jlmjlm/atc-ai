extern int get_ptm(void);
extern int spawn(char *cmd, char *args[], int ptm);
extern void update_display(char);
extern void update_board(void);

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

extern int board_width, board_height;


struct bearing {
    int degree;
    int drow, dcol;
    char key;
    char aircode;
    const char *shortname;
    const char *longname;
};
extern const struct bearing bearings[8];

static inline struct xy apply(int row, int col, int bearing) {
    row += bearings[bearing].drow;
    col += bearings[bearing].dcol;
    struct xy rv = { row, col };
    return rv;
}


struct airport {
    int num;
    int bearing;
    int row, col;
    int trow, tcol;   // target
    int strow1, stcol1, strow2, stcol2;	// secondary targets
    struct xy exc[6]; // exclusion zone
};
extern struct airport airports[AIRPORT_MAX];
extern int n_airports;

struct course {
    struct xyz pos;
    int bearing;
    _Bool cleared_exit;
    struct course *prev, *next;
};

extern struct course *free_course_entry(struct course *);

struct plane {
    char id;
    _Bool isjet;
    _Bool target_airport;
    _Bool bearing_set;
    int target_num;
    struct course *start, *end;
    int start_tm, end_tm;
    struct plane *prev, *next;
};

extern void plot_course(struct plane *, int row, int col, int alt);

// The board's dynamic state.
extern int frame_no;
extern struct plane *plstart, *plend;


extern void order_new_bearing(char id, int bearing);
extern void order_new_altitude(char id, int alt);
extern void land_at_airport(char id, int airport_num);
