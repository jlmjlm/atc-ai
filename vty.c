#include <unistd.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

#include "atc-ai.h"

#define ESC_MAX 20

int screen_height, screen_width;

char *display;

void update_display(char c) {
    static int cur_row, cur_col;
    static int esc_size;
    static char esc[ESC_MAX];

    void display_esc_seq(FILE *out) {
	for (int i = 1; i < esc_size; i++) {
	    if (isgraph(esc[i]))
	        fprintf(out, " %c", esc[i]);
	    else
	        fprintf(out, " \\%o", esc[i]);
	}
    }


    write(1, &c, 1);

    // ESC
    if (c == '\33') {	// An ESC aborts any seqs which are partial
	if (esc_size) {
	    fprintf(logff, "warning: Escape sequence [\\33");
	    display_esc_seq(logff);
	    fprintf(logff, "] aborted by an ESC\n");
	}
	esc[0] = c;
	esc_size = 1;
	return;
    }

    if (esc_size) {	// An escape sequence in progress
	esc[esc_size++] = c;

	if ((esc[1] == '(' || esc[1] == ')') && esc_size == 3) {
 	    // designate charset -- ignore
	    esc_size = 0;
	    return;
	}

	if (esc[1] == '>' && esc_size == 2) {
	    // keypad -- ignore
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
	    display_esc_seq(stderr);
	    fprintf(stderr, "\n");
	    exit('\33');
	}

	return;
    }

    switch (c) {
	// swallow beeps & ^O
	case '\a': case 15: return;

	// CR
	case '\r':
	    cur_col = 0;
	    return;

	// normal text
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
