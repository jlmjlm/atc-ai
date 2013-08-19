#include <stdio.h>
#include <stdbool.h>
#include <assert.h>
#include "atc-ai.h"
#include "pathfind.h"


// Verify the behavior of 'calc_next_move':
//    - If headed for something to the NW, but blocked from the W, head
//	N first.  N at alt e > NE at e > e at e > S,SE at e.
//	No W, NW, or SW.

static void test_blocked() {
    struct plane pl = { .id = 't', .isjet = true, .target_airport = false,
			.bearing_set = true, .target_num = 0,
			.start = NULL, .end = NULL, .start_tm = -1,
			.end_tm = -1, .prev = NULL, .next = NULL };
    int alt = 6;
    struct xyz target = { .row = 0, .col = 0, .alt = 9 };
    int bearing = bearing_of("N");
    struct xy rc = { .row = 5, .col = 5 };
    // c1: NW/W/SW blocked.
    struct course c1 = { .pos = { .row = 5, .col = 3, .alt = alt },
			 .next = &c1, .prev = &c1 };
    // c2: W/NW/N/NE/E blocked.
    struct course c2 = { .pos = { .row = 4, .col = 5, .alt = alt },
			 .next = &c2, .prev = &c2 };
    struct op_courses op = { .c = &c2, .prev = NULL, .next = NULL };
    struct frame fr = { .opc_start = &op, .prev = NULL, .next = NULL };
    
    // Test all moves blocked (c2)
    calc_next_move(&pl, rc.row, rc.col, &alt, target, &bearing,
		   false, &fr);
    assert(fr.n_cand == 0);
    assert(alt < 0);

    alt = 6;
    bearing = bearing_of("N");
    op.c = &c1;

    // Test moves to the west blocked (c1)
    calc_next_move(&pl, rc.row, rc.col, &alt, target, &bearing,
		   false, &fr);

    assert(fr.n_cand == 9);
    for (int i = fr.n_cand-1; i >= 0; i--) {
	printf("#%d: bearing %d at alt %d\n", i, 
	       bearings[fr.cand[i].bearing].degree, fr.cand[i].alt);
    }
    assert(bearing == bearing_of("N"));
    assert(alt == 7);
    struct step s1 = fr.cand[fr.n_cand-1];
    assert(s1.bearing == bearing_of("N"));
    assert(s1.alt == 7);
    int n_checks = 0;
    for (int i = 0; i < fr.n_cand; i++) {
	for (int j = 0; j < fr.n_cand; j++) {
	    if (fr.cand[i].bearing == bearing_of("N") &&
		    fr.cand[i].alt == fr.cand[j].alt &&
		    fr.cand[j].bearing == bearing_of("NE")) {
		assert(i > j);
		n_checks++;
	    }
	    if (fr.cand[i].bearing == bearing_of("NE") &&
		    fr.cand[i].alt == fr.cand[j].alt &&
                    fr.cand[j].bearing == bearing_of("E")) {
		assert(i > j);
		n_checks++;
	    }
	}
    }
    assert(n_checks == 6);
}

static void test_calc_next_move() {
    board_width = 20;
    board_height = 20;

    test_blocked();
}

static inline bool xyz_eq(struct xyz a, struct xyz b) {
    return a.row == b.row && a.col == b.col && a.alt == b.alt;
}

static void test_plot_course() {
    // Test a course where have to backtrack.
    /*     0123456789abc
	  0-------------
	  1|......G....|
	  2|......2....|
	  3|....**3....|
	  4|....*a3....|
	  5|..****3**..|
	  6|..*c*2*b*..|
	  7|..***1***..|
	  8|.....S.....|	
	  9-------------	*/

    board_height = 10;  board_width = 0xD;
    const int srow = 8, scol = 6;
    #define EXC_LEN 7
    struct xyz excr[EXC_LEN] = {
	{ .row = 7, .col = 6, .alt = 1 },
	{ .row = 6, .col = 6, .alt = 2 },
	{ .row = 5, .col = 7, .alt = 3 },
	{ .row = 4, .col = 7, .alt = 3 },
	{ .row = 3, .col = 7, .alt = 3 },
	{ .row = 2, .col = 7, .alt = 2 },
	{ .row = 1, .col = 7, .alt = 1 }
    };
    struct course c1 = { .pos = { .row = 4, .col = 6, .alt = 1 },
			 .prev = &c1, .next = &c1 };
    struct course c2 = { .pos = { .row = 6, .col = 8, .alt = 1 },
			 .prev = &c2, .next = &c2 };
    struct course c3 = { .pos = { .row = 6, .col = 4, .alt = 1 },
			 .prev = &c3, .next = &c3 };
    struct plane pls[4] = {
      { .id = 'a', .start = &c1, .end = &c1, .prev = NULL, .next = &pls[1] },
      { .id = 'b', .start = &c2, .end = &c2, .prev = &pls[0], .next = &pls[2] },
      { .id = 'c', .start = &c3, .end = &c3, .prev = &pls[1], .next = &pls[3] },
      { .id = 's', .isjet = true, .target_airport = true,
	.bearing_set = true, .target_num = 0, .start = NULL, .end = NULL,
	.prev = &pls[2], .next = NULL } };
    plstart = pls;  plend = &pls[3];
    n_airports = 2;
    struct airport G = { .num = 0, .row = 1, .col = 7,
			 .trow = 1, .tcol = 7, .bearing = bearing_of("N") };
    struct airport S = { .num = 1, .row = srow, .col = scol,
			 .bearing = bearing_of("N") };
    for (int i = 0; i < EZ_SIZE; i++) {
	G.exc[i].row = G.exc[i].col = 0;
    }
    airports[0] = G;
    airports[1] = S;

    int alt = 0;
    plot_course(&pls[3], srow, scol, alt);
    struct course *c = pls[3].start;
    for (int i = 0; i < EXC_LEN; i++) {
	assert(c);
	assert(xyz_eq(c->pos, excr[i]));
	c = c->next;
    }
    assert(!c);
}

int testmain() {
    test_calc_next_move();
    test_plot_course();
    printf("PASS\n");
    return 0;
}
