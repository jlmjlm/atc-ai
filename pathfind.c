#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include "atc-ai.h"

int bearing_of(const char *s) {
    for (int i = 0; i < 8; i++) {
	if (!strcmp(bearings[i].shortname, s))
	    return i;
    }
    return -1;
}

static int calc_bearing(bool *bearing_set, int row, int col) {
    *bearing_set = true;
    if ((row == 0 && col == 0) || (row == 1 && col == 1)) 
	return bearing_of("SE");
    if ((row == 0 && col == board_width-1) ||
		(row == 1 && col == board_width-2)) 
	return bearing_of("SW");
    if ((row == board_height-1 && col == 0) ||
		(row == board_height-2 && col == 1)) 
        return bearing_of("NE");
    if ((row == board_height-1 && col == board_width-1) ||
		(row == board_height-2 && col == board_width-2)) 
        return bearing_of("NW");

    *bearing_set = false;
    if (row == 0 || row == 1)
	return bearing_of("S");
    if (col == 0 || col == 1) 
	return bearing_of("E");
    if (row == board_height-1 || row == board_height-2)
	return bearing_of("N");
    if (col == board_width-1 || col == board_width-2)
	return bearing_of("W");

    fprintf(logff, "Warning: Found rogue plane at (%d, %d)!\n", row, col);
    return -1;
}

static void add_course_elem(struct plane *p, int row, int col, int alt,
			    int bearing, bool cleared_exit) {
    fprintf(logff, "\t(%d, %d, %d)@%d\n", row, col, alt, 
	    bearings[bearing].degree);
    struct course *nc = malloc(sizeof(*nc));
    nc->pos.row = row;  nc->pos.col = col;  nc->pos.alt = alt;
    nc->bearing = bearing;
    nc->cleared_exit = cleared_exit;
    if (!p->start)
	p->start = nc;
    nc->prev = p->end;
    nc->next = NULL;
    p->end = nc;
    if (nc->prev) {
	assert(nc->prev->next == NULL);
	nc->prev->next = nc;
    }
}

struct step { int bearing, alt, distance; };

static int distcmp(const void *a, const void *b) {
    const struct step *aa = a;
    const struct step *bb = b;
    return aa->distance - bb->distance;
}

static int cdist(int r, int c, int bearing, int alt, struct xyz target) {
    struct xy rc = apply(r, c, bearing);
    int dr = rc.row - target.row;
    int dc = rc.col - target.col;
    int da = alt - target.alt;
    return dr*dr + dc*dc + da*da;
}

static void calc_next_move(struct plane *p, int row, int col, int *alt, 
			   struct xyz target, int *bearing) {
    //XXX: Avoid obstacles.
    //TODO: Backtrack.
    int turn;  int nalt;
    struct step cand[15];
    int i = 0;
    for (turn = -2; turn <= 2; turn++) {
	int nb = (*bearing + turn) & 7;
	for (nalt = *alt-1; nalt <= *alt+1; nalt++) {
	    cand[i].bearing = nb;
	    cand[i].alt = nalt;
	    cand[i].distance = cdist(row, col, nb, nalt, target);
	    i++;
	}
    }
    assert(i == 15);
    qsort(cand, 15, sizeof(*cand), distcmp);
    *bearing = cand[0].bearing;
    *alt = cand[0].alt;
}

void plot_course(struct plane *p, int row, int col, int alt) {
    assert(alt == 7);	//XXX: handle planes at airports (alt == 0)
    int bearing = calc_bearing(&p->bearing_set, row, col);
    bool cleared_exit = false;
    struct xyz target;

    if (p->target_airport) {
	target.alt = 1;
	target.row = airports[p->target_num].row;
	target.col = airports[p->target_num].col;
    } else {
	target.alt = 9;
	target.row = exits[p->target_num].row;
        target.col = exits[p->target_num].col;
    }
    fprintf(logff, "Plotting course from %d:(%d, %d, %d)@%d to (%d, %d, %d)\n",
	    frame_no, row, col, alt, bearings[bearing].degree, 
	    target.row, target.col, target.alt);
    
    p->start = p->end = NULL;
    add_course_elem(p, row, col, alt, bearing, false);
    p->start_tm = frame_no;
    int tick = frame_no+1;
    while (row != target.row || col != target.col || alt != target.alt) {
	// Plane doesn't move if it's a prop and the tick is odd...
	// ...except that a prop plane in an exit will pop out of it.
	if (!p->isjet && tick%2 == 1 && row != 0 && col != 0 &&
		row != board_height-1 && col != board_width-1) {
	    fprintf(logff, "\t%d:", tick);
	    add_course_elem(p, row, col, alt, bearing, cleared_exit);
	    tick++;
	    continue;
	}
	calc_next_move(p, row, col, &alt, target, &bearing);
        row += bearings[bearing].drow;
        col += bearings[bearing].dcol;
	
	fprintf(logff, "\t%d:", tick);
	add_course_elem(p, row, col, alt, bearing, cleared_exit);
	if (!cleared_exit && row > 2 && row < board_height-3 &&
		col > 2 && col < board_width-3) {
	    cleared_exit = true;
	}
	tick++;
    } 
    if (p->target_airport) {
	if (!p->isjet)
	    add_course_elem(p, row, col, alt, bearing, cleared_exit);
	add_course_elem(p, -1, -1, -2, -1, cleared_exit);
	p->end_tm = tick;
    } else {
	// For an exit, the plane disappears at reaching it.
        p->end_tm = tick-1;
    }
}
