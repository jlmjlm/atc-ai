#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include "atc-ai.h"

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

struct step { int bearing, alt, distance; };

static int distcmp(const void *a, const void *b) {
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
#if 1
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

#else
    if (sgn(rc.col - a->col) == bearings[a->bearing].dcol &&
	    sgn(rc.row - a->row) == bearings[a->bearing].drow &&
	    alt == 1) {
	return true;
    }
#endif
    return false;
}

struct op_courses {
    const struct course *c;
    struct op_courses *prev, *next;
};

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

static void calc_next_move(struct plane *p, int srow, int scol, int *alt, 
			   struct xyz target, int *bearing,
			   struct op_courses *opc_start) {
    // Avoid obstacles.  Obstacles are:  The boundary except for the
    // target exit at alt==9, adjacency with another plane (props have
    // to check this at t+1 and t+2), within 2 of an exit at alt 6-8 if 
    // it's cleared the exit, the exclusion area of an airport at alt <= 2,
    // and matching the bearing/altitude/speed of a blocking airplane (because
    // it'll just continue to block).  [TODO: The last of those]

    //FIXME: Backtrack.
    int turn;  int nalt;
    struct step cand[15];
    int i = 0;

    // If the plane's at the airport, it can only hold or take off.
    if (*alt == 0) {
    	struct xy rc = apply(srow, scol, *bearing);
	if (adjacent_another_plane(rc, 1, opc_start, !p->isjet)) {
	    // Hold.
	    return;
	} else {
	    *alt = 1;
	    return;
	}
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
	    if (adjacent_another_plane(rc, nalt, opc_start, !p->isjet))
		continue;
	    cand[i].bearing = nb;
	    cand[i].alt = nalt;
	    cand[i].distance = cdist(rc.row, rc.col, nalt, target);
	    i++;
	}
    }
    assert(i <= 15);
    if (i == 0)
	fprintf(logff, "Warning: Can't find safe path for plane '%c'\n", p->id);
    qsort(cand, i, sizeof(*cand), distcmp);
    *bearing = cand[0].bearing;
    *alt = cand[0].alt;
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

static void incr_opc(struct op_courses **st, struct op_courses **end) {
    for (struct op_courses *o = *st; o; o = o->next) {
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
    struct op_courses *opc_start = NULL, *opc_end = NULL;
    assert(alt == 7 || alt == 0);
    int bearing = alt ? calc_bearing(&p->bearing_set, row, col)
		      : get_airport_xy(row, col)->bearing;
    bool cleared_exit = false;
    struct xyz target;

    for (struct plane *pi = plstart; pi; pi = pi->next) {
	if (pi == p)
	    continue;
	new_op_course(pi->start, &opc_start, &opc_end);
    }
    incr_opc(&opc_start, &opc_end);

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
    while (row != target.row || col != target.col || alt != target.alt) {
	if (tick > frame_no+400) {
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
	    incr_opc(&opc_start, &opc_end);
	    continue;
	}
	calc_next_move(p, row, col, &alt, target, &bearing, opc_start);
	if (alt) {
            row += bearings[bearing].drow;
            col += bearings[bearing].dcol;
	}
	
	fprintf(logff, "\t%d:", tick);
	add_course_elem(p, row, col, alt, bearing, cleared_exit);
	if (!cleared_exit && row > 2 && row < board_height-3 &&
		col > 2 && col < board_width-3) {
	    cleared_exit = true;
	}
	tick++;
	incr_opc(&opc_start, &opc_end);
    }
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

    for (struct op_courses *o = opc_start; o; o = opc_start) {
	opc_start = o->next;
	free(o);
    }
}
