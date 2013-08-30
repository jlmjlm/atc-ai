#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <assert.h>

#include "atc-ai.h"

int board_width, board_height;
bool skip_tick;
static const char timestr[] = " Time: ";
static const int timesize = sizeof(timestr)-1;

// For when a plane begins in the top-right corner
static const char alttimestr[] = "7Time: ";  

struct exitspec exits[EXIT_MAX];
int n_exits = 0;

struct airport airports[AIRPORT_MAX];
int n_airports = 0;


// The board's dynamic state.
int frame_no = 0;
int n_planes = 0;
struct plane *plstart = NULL, *plend = NULL;


static void handle_new_plane(char code, int row, int col, int alt);
static struct plane *get_plane(char code);

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

static int get_frame_no() {
    int fnum;
    if (memcmp(display + 2*board_width - 1, timestr, timesize) &&
	    memcmp(display + 2*board_width - 1, alttimestr, timesize)) {
	fprintf(stderr, "\nCan't find frame number.\n");
	fprintf(stderr, "Got '%.*s' instead of '%.*s'\n", 
		timesize, display + 2*board_width,
		timesize, timestr);
	exit('t');
    }
    int rv = sscanf(display+(2*board_width-1)+timesize, "%d", &fnum);
    if (rv != 1) {
	fprintf(stderr, "\nCan't read frame number.\n");
	exit('t');
    }
    return fnum;
}

static void board_init() {
    char *spc = memchr(display, 'T', screen_width);
    if (spc == NULL) {
	fprintf(stderr, "\nCan't determine board width.\n");
	exit(' ');
    }
    board_width = spc - display - 1;
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
    int fnum = get_frame_no();
    if (fnum != 1) {
	fprintf(stderr, "\nStarting at frame %d instead of 1.\n", fnum);
	exit('t');
    }

    find_airports();
    fprintf(logff, "Board is %d by %d.\n", board_width, board_height);
}

struct airport *get_airport(int n) {
    for (int i = 0; i < n_airports; i++) {
	if (airports[i].num == n)
	    return &airports[i];
    }
    return NULL;
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

struct course *free_course_entry(struct course *ci) {
    struct course *rv = ci->next;
    assert(ci->prev == NULL);
    if (ci->next)
	ci->next->prev = ci->prev;
    free(ci);
    return rv;
}

void remove_course_entries(struct course *c) {
    while (c) 
	c = free_course_entry(c);
}

struct plane *remove_plane(struct plane *p) {
    struct plane *rv = p->next;
    remove_course_entries(p->start);
    if (p->prev)
	p->prev->next = p->next;
    else {
	assert(plstart == p);
	plstart = p->next;
    }
    if (p->next)
	p->next->prev = p->prev;
    else {
	assert(plend == p);
	plend = p->prev;
    }
    free(p);
    return rv;
}

static void verify_planes() {
    for (struct plane *i = plstart; i; ) {
	assert(i->start_tm == frame_no);
	struct course *next = i->start->next;
	if (!next) {
	    fprintf(logff, "Plane '%c' safe at tick %d frame %d\n",
		    i->id, i->end_tm, frame_no);
	    assert(i->end_tm == frame_no);
	    i = remove_plane(i);
	    continue;
	}
	if (next->pos.alt == -2)
	    land_at_airport(i->id, i->target_num);
	else {
	    if (i->start->bearing != next->bearing)
            	order_new_bearing(i->id, next->bearing);
	    if (i->start->pos.alt != next->pos.alt)
            	order_new_altitude(i->id, next->pos.alt);
	}

	char code = D(i->start->pos.row, i->start->pos.col*2);
	char alt = D(i->start->pos.row, i->start->pos.col*2+1);
	if (i->start->pos.alt == 0) {
	    if (!isdigit(alt) || (!isalpha(code) && !memchr("<>^v", code, 4))) {
	        fprintf(stderr, "\nFound '%c%c' where expected to find a "
				"plane or airport.\n",
		    	code, alt);
	        fprintf(stderr, "[Tick %d] Expected to find plane '%c' at "
		            "(%d, %d) but instead found '%c%c'\n",
		    frame_no, i->id, i->start->pos.row, i->start->pos.col,
		    code, alt);
	        exit('p');
	    }
	} else if (!isalpha(code) || !isdigit(alt)) {
	    fprintf(stderr, "\nFound '%c%c' where expected to find a plane.\n",
		    code, alt);
	    fprintf(stderr, "[Tick %d] Expected to find plane '%c' at "
		            "(%d, %d) but instead found '%c%c'\n",
		    frame_no, i->id, i->start->pos.row, i->start->pos.col,
		    code, alt);
	    exit('p');
	}
	if (code == i->id && alt-'0' != i->start->pos.alt) {
	    fprintf(stderr, "\nFound plane %c at altitude %c=%d where "
		            "expected to find it at altitude %d.\n",
		    code, alt, alt-'0', i->start->pos.alt);
	    fprintf(logff, "Found plane '%c' at altitude %c=%d where "
		           "expected to find it at altitude %d\n",
		    code, alt, alt-'0', i->start->pos.alt);
	    exit('a');
	}
	i = i->next;
    }
}

static const char pls1[] = " pl dt  comm ";
static const char pls2[] = "7pl dt  comm ";

static void check_pldt() {
    char *base = &D(2, 2*board_width-1);
    if (memcmp(base, pls1, sizeof(pls1)-1) &&
	    memcmp(base, pls2, sizeof(pls2)-1)) {
	fprintf(stderr, "\nExpected \"%s\" but found \"%.*s\"\n",
		pls1, sizeof(pls1)-1, base);
	exit('P');
    }
}

static inline bool plane_at_airport(char id) {
    struct plane *p = get_plane(id);
    return p->id == id && p->start && p->start->pos.alt == 0;
}

static void handle_airport_plane(char id, int dtpos) {
    static const char holding[] = ": Holding @ A";
    static const int holdlen = sizeof(holding)-1;
    assert(!memcmp(&D(dtpos, 2*board_width + 5), holding, holdlen));
    char apnum = D(dtpos, 2*board_width + 5 + holdlen);
    struct airport *ap = get_airport(apnum-'0');
    assert(ap);
    handle_new_plane(id, ap->row, ap->col, 0);    
}

static void new_airport_planes() {
    check_pldt();
    for (int i = 3; i < screen_height && D(i, 2*board_width) != '*'; i++) {
	char id = D(i, 2*board_width);
	if (!isalpha(id))
	    continue;
	if (D(i, 2*board_width+1) != '0')
	    continue;
	if (get_plane(id) == NULL) {
	    handle_airport_plane(id, i);
	} else {
	    assert(plane_at_airport(id));
	}
    }
}

static void target(struct plane *p) {
    char id = p->id;
    for (int i = 3; i < screen_height && D(i, 2*board_width) != '*'; i++) {
	if (D(i, 2*board_width) != id)
	    continue;
	char ttype = D(i, 2*board_width+3);
	char tnum = D(i, 2*board_width+4);
	assert(D(i, 2*board_width+5) == ':');
	assert(isdigit(tnum) && (ttype == 'A' || ttype == 'E'));
	p->target_airport = (ttype == 'A');
	p->target_num = tnum-'0';
	return;
    }

    fprintf(stderr, "\nUnable to find the target of plane %c\n", id);
    exit('T');
}

static struct plane *get_plane(char code) {
    for (struct plane *i = plstart; i; i = i->next) {
	if (i->id == code)
	    return i;
    }

    return NULL;
}

static void handle_found_plane(char code, int alt, int row, int col) {
    // See if the plane's ID matches any existing ones.
    struct plane *p = get_plane(code);

    if (p) {
	struct xyz pos = p->start->pos;
	if (pos.alt != alt || pos.row != row || pos.col != col) {
	    fprintf(stderr, "\nExpected to find plane %c at (%d, %d, %d) "
			    "but actually at (%d, %d, %d).\n",
		    code, pos.row, pos.col, pos.alt, row, col, alt);
	    fprintf(logff, "[Tick %d] Expected to find plane '%c' at "
			   "(%d, %d, %d) but actually at (%d, %d, %d)\n",
		    frame_no, code, pos.row, pos.col, pos.alt, row, col, alt);
	    exit('E');
	}
	// Plane's position is A-OK.
	return;
    }

    // It's a new plane.
    if (alt != 7) {
	fprintf(stderr, "\nNew plane %c found at flight level %d != 7.\n",
		code, alt);
	exit('7');
    }
    handle_new_plane(code, row, col, alt);
}

static void handle_new_plane(char code, int row, int col, int alt) {
    fprintf(logff, "New plane '%c' found at (%d, %d, %d) on turn %d.\n",
	    code, row, col, alt, frame_no);
    struct plane *p = malloc(sizeof(*p));
    p->id = code;
    p->isjet = islower(code);
    target(p);
    plot_course(p, row, col, alt);
    if (p->start) {
	assert(p->start->pos.alt == alt);
	assert(p->start->pos.row == row);
	assert(p->start->pos.col == col);
	struct course *next = p->start->next;
	if (next) {
	    if (alt)
                order_new_bearing(p->id, next->bearing);
	    p->bearing_set = true;
	    if (next->pos.alt != alt)
	        order_new_altitude(p->id, next->pos.alt);
	}
    }

    p->next = NULL;
    p->prev = plend;
    if (plend) {
	plend->next = p;
    } else {
	plstart = p;
    }
    plend = p;
}

static void find_new_planes() {
    int r, c;
    for (r = 0; r < board_height; r++) {	
	for (c = 0; c < board_width; c++) {
	    char code = D(r, 2*c);
	    char alt = D(r, 2*c+1);
	    if (!isalpha(code))
		continue;
	    if (!isdigit(alt)) {
		fprintf(stderr, "\n\"Plane\" '%c%c' has bad altitude.\n",
			code, alt);
		exit('A');
	    }
	    handle_found_plane(code, alt-'0', r, c);
	}
    }
}

static void update_plane_courses() {
    for (struct plane *p = plstart; p; p = p->next) {
	p->start = free_course_entry(p->start);
	p->start_tm++;
    }
}

void update_board() {
    if (frame_no == 0)
	board_init();

    int new_frame_no = get_frame_no();
    if (new_frame_no == frame_no) {
	// Clock hasn't ticked, no-op.
	return;
    }

    if (new_frame_no != frame_no+1) {
	fprintf(stderr, "\nFrame number jumped from %d to %d.\n", frame_no,
		new_frame_no);
	exit('f');
    }

    if (frame_no <= 3)
	check_for_exits();

    frame_no = new_frame_no;
    verify_planes();
    find_new_planes();
    new_airport_planes();
    update_plane_courses();
    if (skip_tick)
        next_tick();

    if (frame_no % 1000 == 0) {
	fprintf(logff, "n_malloc = %d; n_free = %d; difference = %d\n",
		n_malloc, n_free, n_malloc - n_free);
    }
}
