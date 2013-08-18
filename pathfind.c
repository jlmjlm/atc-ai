#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include "atc-ai.h"
#include "pathfind.h"


const struct bearing bearings[8] = {
    { 0, -1, 0, 'w', '^', "N", "north" },
    { 45, -1, 1, 'e', '\0', "NE", "northeast" },
    { 90, 0, 1, 'd', '>', "E", "east" },
    { 135, 1, 1, 'c', '\0', "SE", "southeast" },
    { 180, 1, 0, 'x', 'v', "S", "south" },
    { 225, 1, -1, 'z', '\0', "SW", "southwest" },
    { 270, 0, -1, 'a', '<', "W", "west" },
    { 315, -1, -1, 'q', '\0', "NW", "northwest" },
};  


static inline int sgn(int x) {
    if (x > 0)
	return 1;
    if (x < 0)
	return -1;
    return 0;
}

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

static int distcmp(const void *b, const void *a) {
    const struct step *aa = a;
    const struct step *bb = b;
    return aa->distance - bb->distance;
}

static int cdist(int r, int c, int alt, struct xyz target) {
    int dr = r - target.row;
    int dc = c - target.col;
    int da = alt - target.alt;
    return dr*dr + dc*dc + da*da;
}

static bool in_airport_excl(struct xy rc, int alt, int airport_num) {
    struct airport *a = get_airport(airport_num);

    if (alt >= 3)
	return false;

    for (int i = 0; i < EZ_SIZE; i++) {
        if (a->exc[i].row == rc.row && a->exc[i].col == rc.col)
            return true;
    }

    if (bearings[a->bearing].dcol)
	return sgn(rc.col - a->col) == bearings[a->bearing].dcol;
    else
	return sgn(rc.row - a->row) == bearings[a->bearing].drow;
}

static bool pos_adjacent(struct xyz pos, struct xy rc, int alt) {
    if (pos.alt == 0)
	return false;

    return abs(pos.row - rc.row) < 2 && abs(pos.col - rc.col) < 2 &&
	   abs(pos.alt - alt) < 2;
}

static bool adjacent_another_plane(struct xy rc, int alt,
				   const struct op_courses *opc, bool isprop) {
    for ( ; opc; opc = opc->next) {
	if (!opc->c)
	    continue;
	struct xyz pos = opc->c->pos;
	if (pos_adjacent(pos, rc, alt))
	    return true;
	if (isprop && opc->c->next) {
	    pos = opc->c->next->pos;
	    if (pos_adjacent(pos, rc, alt))
		return true;
	}
    }
    return false;
}

void calc_next_move(struct plane *p, int srow, int scol, int *alt, 
		    struct xyz target, int *bearing, bool cleared_exit,
		    struct frame *frame) {
    // Avoid obstacles.  Obstacles are:  The boundary except for the
    // target exit at alt==9, adjacency with another plane (props have
    // to check this at t+1 and t+2), within 2 of an exit at alt 6-8 if 
    // it's cleared the exit, the exclusion area of an airport at alt <= 2,
    // and matching the bearing/altitude/speed of a blocking airplane (because
    // it'll just continue to block).  [TODO: The last of those]

    int turn;  int nalt;

    // If the plane's at the airport, it can only hold or take off.
    if (*alt == 0) {
    	struct xy rc = apply(srow, scol, *bearing);
	frame->cand[0].bearing = frame->cand[1].bearing = *bearing;
	frame->cand[0].alt = 0;  frame->cand[1].alt = 1;
	if (adjacent_another_plane(rc, 1, frame->opc_start, !p->isjet)) {
	    // Can't take off, can only hold.
	    frame->n_cand = 1;
	} else {
	    *alt = 1;
	    frame->n_cand = 2;
	}
	return;
    }
   
    for (turn = -2; turn <= 2; turn++) {
	int nb = (*bearing + turn) & 7;
    	struct xy rc = apply(srow, scol, nb);
	if (rc.row < 0 || rc.col < 0 ||
	        rc.row >= board_height || rc.col >= board_width)
	    continue;
	bool on_boundary = (rc.row == 0 || rc.row == board_height-1 ||
			    rc.col == 0 || rc.col == board_width-1);
	for (nalt = *alt-1; nalt <= *alt+1; nalt++) {
	    if (nalt == 0 || nalt == 10)
		continue;
	    if (on_boundary && (nalt != 9 || rc.row != target.row ||
					     rc.col != target.col))
		continue;
	    if (p->target_airport && in_airport_excl(rc, nalt, p->target_num))
		continue;
	    if (adjacent_another_plane(rc, nalt, frame->opc_start, !p->isjet))
		continue;
	    /*if (cleared_exit && nalt >= 6 && nalt <= 8 && (
		    rc.row <= 2 || rc.row >= board_height - 3 ||
		    rc.col <= 2 || rc.col >= board_width - 3))
		continue;*/
	    int i = frame->n_cand++;
	    frame->cand[i].bearing = nb;
	    frame->cand[i].alt = nalt;
	    frame->cand[i].distance = cdist(rc.row, rc.col, nalt, target);
	}
    }
    assert(frame->n_cand <= 15);
    if (frame->n_cand == 0) {
	fprintf(logff, "Warning: Can't find safe path for plane '%c'\n", p->id);
	*alt = -1;
	return;
    }
    qsort(frame->cand, frame->n_cand, sizeof(*frame->cand), distcmp);
    *bearing = frame->cand[frame->n_cand].bearing;
    *alt = frame->cand[frame->n_cand].alt;
}

static void new_op_course(const struct course *c,
			  struct op_courses **st,
			  struct op_courses **end) {
    struct op_courses *ne = malloc(sizeof(*ne));
    ne->c = c;
    ne->prev = *end;
    ne->next = NULL;
    if (*end)
	(**end).next = ne;
    else
	*st = ne;
    *end = ne;
}

static void incr_opc(struct op_courses *st) {
    for (struct op_courses *o = st; o; o = o->next) {
        if (o->c)
    	    o->c = o->c->next;
    }
}

static struct airport *get_airport_xy(int r, int c) {
    for (int i = 0; i < n_airports; i++) {
	if (airports[i].row == r && airports[i].col == c)
	    return &airports[i];
    }

    return NULL;
}

void plot_course(struct plane *p, int row, int col, int alt) {
    struct frame *frstart = malloc(sizeof *frstart);
    struct frame *frend = frstart;
    frstart->prev = frstart->next = NULL;
    frstart->opc_start = NULL;
    struct op_courses *opc_end = NULL;

    assert(alt == 7 || alt == 0);
    int bearing = alt ? calc_bearing(&p->bearing_set, row, col)
		      : get_airport_xy(row, col)->bearing;
    bool cleared_exit = false;
    struct xyz target;

    for (struct plane *pi = plstart; pi; pi = pi->next) {
	if (pi == p)
	    continue;
	new_op_course(pi->start, &frstart->opc_start, &opc_end);
    }
    incr_opc(frstart->opc_start);

    if (p->target_airport) {
	target.alt = 1;
	target.row = get_airport(p->target_num)->trow;
	target.col = get_airport(p->target_num)->tcol;
    } else {
	target.alt = 9;
	target.row = get_exit(p->target_num)->row;
        target.col = get_exit(p->target_num)->col;
    }
    fprintf(logff, "Plotting course from %d:(%d, %d, %d)@%d to (%d, %d, %d)\n",
	    frame_no, row, col, alt, bearings[bearing].degree, 
	    target.row, target.col, target.alt);
    
    p->start = p->end = NULL;
    add_course_elem(p, row, col, alt, bearing, false);
    p->start_tm = frame_no;
    int tick = frame_no+1;
    int steps = 0;

    /* Operation of the "plotting course" machine:
     *	  (A) Get a frame for the current pos'n.
     * 	  (B) If frame has cands, step ahead to the best cand and return to (A).
     *    (C) If not, step back to parent frame and remove the cand and
     *	      return to (B).
     */
    get_frame:
	if (++steps > 200) {
	    fprintf(stderr, "\nPlane %c stuck in an infinite loop.\n", p->id);
	    exit('8');
	}
	

	// Plane doesn't move if it's a prop and the tick is odd...
	// ...except that a prop plane in an exit will pop out of it.
	if (!p->isjet && tick%2 == 1 && row != 0 && col != 0 &&
		row != board_height-1 && col != board_width-1) {
	    fprintf(logff, "\t%d:", tick);
	    add_course_elem(p, row, col, alt, bearing, cleared_exit);
	    tick++;
	    incr_opc(frstart->opc_start);//XXX
	    //XXXcontinue;
	}
	calc_next_move(p, row, col, &alt, target, &bearing, cleared_exit,
		       frstart);
	if (alt) {
            row += bearings[bearing].drow;
            col += bearings[bearing].dcol;
	}
    //while (row != target.row || col != target.col || alt != target.alt) {
	
	fprintf(logff, "\t%d:", tick);
	add_course_elem(p, row, col, alt, bearing, cleared_exit);
	if (!cleared_exit && ((row > 2 && row < board_height-3 &&
		col > 2 && col < board_width-3) || alt < 6 || alt == 9)) {
	    cleared_exit = true;
	}
	tick++;
	incr_opc(frstart->opc_start);
    //}
    if (p->target_airport) {
	if (!p->isjet) {
	    add_course_elem(p, row, col, alt, bearing, cleared_exit);
	    tick++;
 	}
	add_course_elem(p, -1, -1, -2, -1, cleared_exit);
	p->end_tm = tick;
    } else {
	// For an exit, the plane disappears at reaching it.
        p->end_tm = tick-1;
    }

    for (struct op_courses *o = frstart->opc_start; o; o = frstart->opc_start) {
	frstart->opc_start = o->next;
	free(o);
    }
}
