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
	    int rv;

	    switch (c) {
		default:
		    fprintf(logff, "warning: Unknown escape sequence [\\33");
		    display_esc_seq(logff);
		    fprintf(logff, "], ignoring.\n");
		    break;
		case 'm': // display attributes (inverse, bold, etc.) -- ignore
		case 'r': // scrolling region -- ignore
		case 'h': // terminal mode -- ignore
		case 'l': // terminal mode reset -- ignore
		case 'J': case 'K': // erase -- ignore
		    break;
		case 'H': // cursor position
		    if (esc_size == 3) {
			cur_row = cur_col = 0;
			break;
		    }
		    rv = sscanf(esc+2, "%d;%d", &cur_row, &cur_col);
		    if (rv != 2) {
			fprintf(stderr,
			        "\nInvalid cursor position sequence [\\33");
			display_esc_seq(stderr);
			fprintf(stderr, "]\n");
			exit('H');
		    }
		    // VT100 positions are 1-origin, not 0-origin.
		    cur_row--; cur_col--;
		    break;
		case 'A': // move cursor up
		case 'B': // move cursor down
		case 'C': // move cursor right
		case 'D':; // move cursor left
		    int drow[4] = { -1, 1, 0, 0 };
		    int dcol[4] = { 0, 0, 1, -1 };
		    rv = 1;
		    sscanf(esc+2, "%d", &rv);
		    cur_row += rv * drow[c-'A'];
		    cur_col += rv * dcol[c-'A'];
		    if (cur_row >= screen_height)
			cur_row = screen_height-1;
		    if (cur_row < 0)
			cur_row = 0;
		    if (cur_col >= screen_width)
			cur_col = screen_width-1;
		    if (cur_col < 0)
			cur_col = 0;
		    break;
	    }
    
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

	// BS
	case '\b':
	    cur_col--;
	    if (cur_col < 0)
		cur_col = 0;
	    return;

	// normal text
	default:
    	    D(cur_row, cur_col) = c;
	    if (cur_col+1 < screen_width) {
    	    	cur_col++;
		return;
	    }
	    // ... else wrap to next line
	    cur_col = 0;
	    // Fallthrough

	// LF
	case '\n':
	    if (cur_row+1 < screen_height)
		cur_row++;
	    return;
    }
}
