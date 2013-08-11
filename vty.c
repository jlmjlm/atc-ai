#include <unistd.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

#include "atc-ai.h"

#define ESC_MAX 20

int screen_height, screen_width;

char *display;

void update_display(char c) {
    write(1, &c, 1);

    static int cur_row, cur_col;
    static int esc_size;
    static char esc[ESC_MAX];

    if (esc_size) {	// An escape sequence in progress
	esc[esc_size++] = c;

	if ((esc[1] == '(' || esc[1] == ')') && esc_size == 3) {
 	    // designate charset -- ignore
	    esc_size = 0;
	    return;
	}
	
	if (esc[1] == '[' && isalpha(c)) {
	    // control sequence
	    // XXX
	    esc_size = 0;
	    return;
	}

	if (esc_size == ESC_MAX) {
	    fprintf(stderr, "\nUnknown escape sequence \\33");
	    for (int i = 0; i < ESC_MAX; i++) {
	   	if (isgraph(esc[i]))
		    fprintf(stderr, " %c", esc[i]);
		else
		    fprintf(stderr, " \\%o", esc[i]);
	    }
	    fprintf(stderr, "\n");
	    exit('\33');
	}

	return;
    }

    switch (c) {
	// swallow beeps & ^O
	case '\a': case 15: return;

	// CR
	case '\r': cur_col = 0; return;

	// ESC
	case '\33':
	    esc[0] = c;
	    esc_size = 1;
	    return;

	default:
    	    D(cur_row, cur_col) = c;
	    if (cur_col+1 < screen_width) {
    	    	cur_col++;
		return;
	    }
	    cur_col = 0;
	    // Fallthrough

	// LF
	case '\n':
	    if (cur_row+1 < screen_height)
		cur_row++;
	    return;
    }
}

void output_display(int fd) {
    write(fd, display, screen_height*screen_width);
}
