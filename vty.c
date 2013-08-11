#include <unistd.h>

#include "atc-ai.h"

#define ESC_MAX 20

int screen_height, screen_width;

char *display;

void update_display(char c) {
    static int cur_row, cur_col;
    static int esc_size;
    static char esc[ESC_MAX];

    // swallow beeps
    if (c == '\a')
	return;

    D(cur_row, cur_col) = c;
    cur_col++;
    if (cur_col == screen_height*screen_width)
	cur_col = 0;
}

void output_display(int fd) {
    write(fd, display, screen_height*screen_width);
}
