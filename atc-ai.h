extern int get_ptm(void);
extern int spawn(char *cmd, char *args[], int ptm);
extern void update_display(char);
extern void output_display(int fd);

extern int screen_height, screen_width;
extern char *display;

#define D(row, col) (display[(row)*screen_width + (col)])
