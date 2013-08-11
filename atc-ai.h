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

struct exitspec {
    int num;
    int row, col;
};
extern struct exitspec exits[EXIT_MAX];
extern int n_exits;
extern struct exitspec *get_exit(int);
