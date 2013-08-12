#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "atc-ai.h"

static int board_width, board_height;
static int frame_no;
static const char timestr[] = " Time: ";
static const int timesize = sizeof(timestr)-1;

struct exitspec exits[EXIT_MAX];
int n_exits = 0;

struct airport airports[AIRPORT_MAX];
int n_airports = 0;


static int get_bearing(char code) {
    for (int i = 0; i < 8; i++) {
	if (bearings[i].aircode == code)
	    return i;
    }
    return -1;
}

static void new_airport(int row, int col, int bearing, int id) {
    if (n_airports == AIRPORT_MAX) {
	fprintf(stderr, "\nToo many airports.\n");
	exit('a');
    }
    struct airport *airport = airports + (n_airports++);
    airport->num = id;
    airport->bearing = bearing;
    airport->row = row;
    airport->col = col;
    struct xy xy = apply(row, col, (bearing+4)&7);
    airport->trow = xy.row;
    airport->tcol = xy.col;
    for (int i = -2; i <= 2; i++) {
	airport->exc[i+2] = apply(row, col, (bearing+i)&7);
    }
    airport->exc[5].row = row;
    airport->exc[5].col = col;
    fprintf(logff, "Found airport #%d at (%d, %d) bearing %s\n",
	    id, row, col, bearings[bearing].longname);
}

static void find_airports() {
    int r, c;
    for (r = 1; r < board_height-1; r++) {
	for (c = 1; c < board_width-1; c++) {
	    if (isdigit(D(r, 2*c+1)) && D(r, 2*c) != '*' &&
		    !isalpha(D(r, 2*c))) {
		fprintf(logff, "Found '%c%c' at (%d, %d)\n",
			D(r, 2*c), D(r, 2*c+1), r, c);
		int bearing = get_bearing(D(r, 2*c));
		new_airport(r, c, bearing, D(r, 2*c+1) - '0');
	    }
	}
    }
    fprintf(logff, "D(board_height-3, board_width-10) = '%c%c'\n",
	    D(board_height-3, (board_width-10)*2),
	    D(board_height-3, (board_width-10)*2+1));
    r = board_height-3;
    c = board_width-10;
    fprintf(logff, "r = %d; c = %d; D(r, 2*c) = '%c'; D(r, 2*c+1) = '%c'; "
		   "isdigit(D(r, 2*c+1)) = %d\n", r, c, D(r, 2*c),
		   D(r, 2*c+1), isdigit(D(r, 2*c+1)));
}

static void board_init() {
    char *spc = memchr(display, ' ', screen_width);
    if (spc == NULL) {
	fprintf(stderr, "\nCan't determine board width.\n");
	exit(' ');
    }
    board_width = spc - display;
    if (board_width % 2 == 0) {
	fprintf(stderr, "\nInvalid width of %d.5 chars.\n", board_width/2);
	exit(2);
    }
    board_width = (board_width + 1) / 2;
    if (board_width <= 5) {
	fprintf(stderr, "\nBoard unreasonably thin.\n");
	exit(board_width+1);
    }
    if (board_width > 80) {
	fprintf(stderr, "\nBoard unreasonably wide.\n");
	exit(board_width);
    }
    for (int i = 1; i < screen_height; i++) {
	if (D(i, 0) == ' ') {
    	    board_height = i;
	    break;
	}
    }
    if (board_height == 0) {
	fprintf(stderr, "\nCannot determine board height.\n");
	exit(' ');
    }
    if (board_height <= 5) {
	fprintf(stderr, "\nBoard unreasonably short.\n");
	exit(board_height+1);
    }
    if (board_height > 50) {
	fprintf(stderr, "\nBoard unreasonably tall.\n");
	exit(board_height);
    }
    if (D(board_height-1, 0) != '-' && D(board_height-1, 2) != '-') {
	fprintf(stderr, "\nCan't find lower left corner of board.\n");
	exit('L');
    }
    if (memcmp(display + 2*board_width - 1, timestr, timesize)) {
	fprintf(stderr, "\nCan't find frame number.\n");
	fprintf(stderr, "Got '%.*s' instead of '%.*s'\n", 
		timesize, display + 2*board_width,
		timesize, timestr);
	exit('t');
    }
    int rv = sscanf(display+(2*board_width-1)+timesize, "%d", &frame_no);
    if (rv != 1) {
	fprintf(stderr, "\nCan't read frame number.\n");
	exit('t');
    }
    if (frame_no != 1) {
	fprintf(stderr, "\nStarting at frame %d instead of 1.\n", frame_no);
	exit('t');
    }

    find_airports();
    fprintf(logff, "Board is %d by %d.\n", board_width, board_height);
}

struct exitspec *get_exit(int n) {
    for (int i = 0; i < n_exits; i++) {
	if (exits[i].num == n)
	    return &exits[i];
    }
    return NULL;
}

static void new_exit(int row, int col) {
    int exit_num = D(row, 2*col) - '0';
    struct exitspec *spec = get_exit(exit_num);
    if (spec && (spec->row != row || spec->col != col)) {
	fprintf(stderr, "\nconflict: exit %d found at both (%d, %d) "
		        "and (%d, %d)\n", exit_num, spec->row, spec->col,
			row, col);
	exit('c');
    }
    if (spec)
	return;
    spec = &exits[n_exits++];
    if (n_exits > EXIT_MAX) {
	fprintf(stderr, "\nToo many exits found.\n");
	exit('e');
    }
    spec->num = exit_num; spec->row = row; spec->col = col;
    fprintf(logff, "Found exit %d at (%d, %d)\n", exit_num, row, col);
}

static void check_for_exits() {
    // Exits on N and S borders are of the form "#-"
    int i;
    for (i = 0; i < board_width-1; i++) {
	if (isdigit(D(0, i*2)) && D(0, i*2+1) == '-')
	    new_exit(0, i);
	if (isdigit(D(board_height-1, i*2)) && D(board_height-1, i*2+1) == '-')
	    new_exit(board_height-1, i);
    }

    // Exits on E and W borders are of the form "# "
    for (i = 0; i < board_height; i++) {
	if (isdigit(D(i, 0)) && D(i, 1) == ' ')
	    new_exit(i, 0);
	if (isdigit(D(i, 2*board_width-2)) && D(i, 2*board_width-1) == ' ')
	    new_exit(i, board_width-1);
    }
}

void update_board() {
    if (frame_no == 0)
	board_init();

    if (frame_no <= 3)
	check_for_exits();

    // XXX Verify everything is where we expect it, and check for new planes.
}