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

void plot_course(struct plane *p, int row, int col, int alt) {
    assert(alt == 7);	//XXX: handle planes at airports (alt == 0)
    int bearing = calc_bearing(&p->bearing_set, row, col);
    int drow = bearings[bearing].drow;
    int dcol = bearings[bearing].dcol;
    bool cleared_exit = false;
    
    p->start = p->end = NULL;
    p->start_tm = frame_no;
    int tick = frame_no;
    do {
	add_course_elem(p, row, col, alt, bearing, cleared_exit);
	// move plane unless it's a prop and the tick is even.
	if (p->isjet || tick%2 == 1) {
	    row += drow;  
	    col += dcol;   
	}
	if (!cleared_exit && row > 2 && row < board_height-3 &&
		col > 2 && col < board_width-3) {
	    cleared_exit = true;
	}
	tick++;
    } while (row >= 0 && col >= 0 && row < board_height && col < board_width);
    p->end_tm = tick-1;
}
