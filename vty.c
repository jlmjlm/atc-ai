#include <unistd.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "atc-ai.h"

#define ESC_MAX 20

int screen_height, screen_width;
static int saved_row, saved_col;
static int sr_start, sr_end;    // The scroll region.

char *display;


static void scroll_up() {
    char *start = &D(sr_start, 0);
    char *from = &D(sr_start+1, 0);
    char *end = &D(sr_end, 0);

    memmove(start, from, end-start);
    for (int i = 0; i < screen_width; i++)
        D(sr_end, i) = ' ';

    if (verbose) {
        fprintf(logff, "Scrolled up.  New display:\n%.*s\n",
                screen_height*screen_width, display);
    }
}

static void scroll_down() {
    char *start = &D(sr_start+1, 0);
    char *from = &D(sr_start, 0);
    char *end = &D(sr_end, 0);

    memmove(start, from, end-from);
    for (int i = 0; i < screen_width; i++)
        D(sr_start, i) = ' ';

    if (verbose) {
        fprintf(logff, "Scrolled down.  New display:\n%.*s\n",
                screen_height*screen_width, display);
    }
}


__attribute__((format(printf, 1, 2) ))
static inline void trace(const char *fmt, ...) {
    static const bool do_trace = false;

    if (do_trace) {
        va_list va;
        va_start(va, fmt);
        vfprintf(logff, fmt, va);
        va_end(va);
    }
}

void update_display(char c) {
    static int cur_row, cur_col;
    static bool at_sr_bottom;
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

    if (sr_end == 0)
        sr_end = screen_height-1;

    // ESC
    if (c == '\33') {
        if (esc_size) {  // An ESC aborts any seqs which are partial
            fprintf(logff, "warning: Escape sequence [\\33");
            display_esc_seq(logff);
            fprintf(logff, "] aborted by an ESC\n");
        }
        esc[0] = c;
        esc_size = 1;
        return;
    }

    if (esc_size) {     // An escape sequence in progress
        esc[esc_size++] = c;

        if ((esc[1] == '(' || esc[1] == ')') && esc_size == 3) {
            // designate charset -- ignore
            trace("ignoring: %.*s\n", esc_size, esc);
            esc_size = 0;
            return;
        }

        if (esc[1] == '>' && esc_size == 2) {
            // keypad -- ignore
            trace("ignoring: %.*s\n", esc_size, esc);
            esc_size = 0;
            return;
        }

        if (esc[1] == 'D' && esc_size == 2) {
            trace("index: %.*s\n", esc_size, esc);
            if (cur_row == sr_end)
                scroll_up();
            else if (cur_row < screen_height-1)
                cur_row++;
            esc_size = 0;
            return;
        }

        if (esc[1] == 'M' && esc_size == 2) {
            trace("reverse index: %.*s\n", esc_size, esc);
            if (cur_row == sr_start)
                scroll_down();
            else if (cur_row > 0)
                cur_row--;
            esc_size = 0;
            return;
        }

        if (esc[1] == '7' && esc_size == 2) {
            saved_row = cur_row;
            saved_col = cur_col;
            esc_size = 0;
            return;
        }

        if (esc[1] == '8' && esc_size == 2) {
            cur_row = saved_row;
            cur_col = saved_col;
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
                case 'h': // terminal mode -- ignore
                case 'l': // terminal mode reset -- ignore
                case 'J': case 'K': // erase -- ignore
                    trace("ignoring: %.*s\n", esc_size, esc);
                    break;
                case 'r': // scrolling region
                    if (esc_size == 3) {
                        trace("setting default scroll region "
                              "(entire display)\n");
                        sr_start = 0;
                        sr_end = screen_height-1;
                        cur_row = cur_col = 0;
                        at_sr_bottom = false;
                        break;
                    }
                    rv = sscanf(esc+2, "%d;%d", &sr_start, &sr_end);
                    if (rv != 2) {
                        cleanup();
                        fprintf(stderr, "Invalid scroll region sequence [\\33");
                        display_esc_seq(stderr);
                        fprintf(stderr, "]\n");
                        exit('r');
                    }
                    // VT100 positions are 1-origin, not 0-origin.
                    sr_start--;  sr_end--;
                    cur_row = cur_col = 0;
                    at_sr_bottom = false;
                    trace("setting scroll region to %d-%d: %.*s\n",
                          sr_start, sr_end, esc_size, esc);
                    break;
                case 'H': // cursor position
                    if (esc_size == 3) {
                        trace("going to (0, 0): %.*s\n", esc_size, esc);
                        cur_row = cur_col = 0;
                        at_sr_bottom = false;
                        break;
                    }
                    rv = sscanf(esc+2, "%d;%d", &cur_row, &cur_col);
                    if (rv != 2) {
                        cleanup();
                        fprintf(stderr,
                                "Invalid cursor position sequence [\\33");
                        display_esc_seq(stderr);
                        fprintf(stderr, "]\n");
                        exit('H');
                    }
                    // VT100 positions are 1-origin, not 0-origin.
                    cur_row--; cur_col--;
                    trace("going to (%d, %d): %.*s\n", cur_row, cur_col,
                          esc_size, esc);
                    at_sr_bottom = false;
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
                    trace("going to (%d, %d): %.*s\n", cur_row, cur_col,
                          esc_size, esc);
                    at_sr_bottom = false;
                    break;
            }

            esc_size = 0;
            return;
        }

        if (esc_size == ESC_MAX) {
            cleanup();
            fprintf(stderr, "Unknown escape sequence \\33");
            display_esc_seq(stderr);
            fprintf(stderr, "\n");
            exit('\33');
        }

        return;
    }

    switch (c) {
        // swallow beeps & ^O
        case '\a': case 15:
            trace("ignoring: %c\n", c);
            return;

        // CR
        case '\r':
            cur_col = 0;
            trace("carriage return: going to (%d, %d)\n", cur_row, cur_col);
            at_sr_bottom = false;
            return;

        // LF
        case '\n':
            if (cur_row == sr_end)
                scroll_up();
            else if (cur_row+1 < screen_height)
                cur_row++;
            at_sr_bottom = false;
            trace("line feed: going to (%d, %d)\n", cur_row, cur_col);
            return;

        // BS
        case '\b':
            cur_col--;
            if (cur_col < 0)
                cur_col = 0;
            trace("going to (%d, %d): %c\n", cur_row, cur_col, c);
            at_sr_bottom = false;
            return;

        // normal text
        default:
            if (!isprint(c)) {
                fprintf(logff, "Warning: Got unhandled non-printable "
                               "character \\%o\n", c);
            }
            if (at_sr_bottom) {
                scroll_up();
                at_sr_bottom = false;
            }
            D(cur_row, cur_col) = c;
            trace("setting (%d, %d) to '%c' and ", cur_row, cur_col, c);
            if (cur_col+1 < screen_width) {
                cur_col++;
                trace("going to (%d, %d)\n", cur_row, cur_col);
                return;
            }
            // ... else wrap to next line
            cur_col = 0;
            if (cur_row == sr_end)
                at_sr_bottom = true;
            else if (cur_row+1 < screen_height)
                cur_row++;
            trace("going to (%d, %d)\n", cur_row, cur_col);
            return;
    }
}
