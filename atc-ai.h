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

struct bearing {
    int degrees;
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
    struct xy exc[6]; // exclusion zone
};
extern struct airport airports[AIRPORT_MAX];
extern int n_airports;
