#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <assert.h>
#include "atc-ai.h"
#include "pathfind.h"

static void check_course(struct course *c, struct xyz *excr, int exlen,
			 bool isprop);


// Verify the behavior of 'calc_next_move':
//    - If headed for something to the NW, but blocked from the W, head
//	N first.  N at alt e > NE at e > E at e > S,SE at e.
//	No W, NW, or SW.

static void test_blocked() {
    struct plane pl = { .id = 't', .isjet = true, .target_airport = false,
			.target_num = 0, .start = NULL, .end = NULL,
		        .start_tm = -1, .end_tm = -1,
		        .prev = NULL, .next = NULL };
    int alt = 6;
    struct xyz target = { .row = 0, .col = 0, .alt = 9 };
    int bearing = bearing_of("N");
    struct xy rc = { .row = 5, .col = 5 };
    // c1: NW/W/SW blocked.
    struct course c1 = { .pos = { .row = 5, .col = 3, .alt = alt },
			 .bearing = -1, .next = &c1, .prev = &c1 };
    // c2: W/NW/N/NE/E blocked.
    struct course c2 = { .pos = { .row = 4, .col = 5, .alt = alt },
			 .bearing = -1, .next = &c2, .prev = &c2 };
    struct op_courses op = { .c = &c2, .isjet = false,
			     .prev = NULL, .next = NULL };
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
	fprintf(logff, "#%d: bearing %d at alt %d\n", i, 
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

    //printf("n_malloc = %d; n_free = %d\n", n_malloc, n_free);
    assert(n_malloc == 0);
    assert(n_free == 0);
}

// Test the matchcourse penalty:  Verify a jet ('i') at (1, 11, 9) bearing
// west prevents a jet ('j') at (1, 9, 9) from going west.
static void test_matchcourse() {
    struct plane pi = { .id = 'i', .isjet = true, .target_airport = false,
			.target_num = 0, .start = NULL, .end = NULL,
			.start_tm = -1, .end_tm = -1,
                        .prev = NULL, .next = NULL };
    struct plane pj = { .id = 'j', .isjet = true, .target_airport = false,
			.target_num = 1, .start = NULL, .end = NULL,
			.start_tm = -1, .end_tm = -1,
                        .prev = NULL, .next = NULL };
    plstart = plend = &pi;
    struct xyz target = { .row = 0, .col = 19, .alt = 9 };
    int bearing = bearing_of("W");
    struct xy rc = { .row = 1, .col = 9 };
    int jalt = 9;
    struct course c1 = { .pos = { .row = 1, .col = 11, .alt = 9 },
			 .bearing = bearing, .prev = NULL };
    struct course c2 = { .pos = { .row = 1, .col = 10, .alt = 9 },
			 .bearing = bearing, .next = NULL, .prev = &c1 };
    c1.next = &c2;
    pi.start = &c1;  pi.end = &c2;
    struct op_courses op = { .c = &c2, .isjet = true, 
			     .prev = NULL, .next = NULL };
    struct frame fr = { .opc_start = &op, .prev = NULL, .next = NULL };

    calc_next_move(&pj, rc.row, rc.col, &jalt, target, &bearing, true, &fr);
    assert(fr.n_cand > 0);
    printf("Bearing %s\n", bearings[bearing].longname); //XXX
    assert(bearing == bearing_of("SW"));
    assert(jalt == 9);
    struct step s1 = fr.cand[fr.n_cand-1];
    assert(s1.bearing == bearing_of("SW"));
    assert(s1.alt == 9);
}

static void test_calc_next_move() {
    board_width = 20;
    board_height = 20;

    test_blocked();
    test_matchcourse();
}

static inline bool xyz_eq(struct xyz a, struct xyz b) {
    return a.row == b.row && a.col == b.col && a.alt == b.alt;
}

static void test_excl_landing(int alt, int exp_n_cands) {
    board_width = board_height = 10;
    const int srow = 6, scol = 5;
    n_airports = 1;
    struct airport G = { .num = 0, .row = 6, .col = 5, .trow = 5, .tcol = 5,
			 .strow1 = 5, .stcol1 = 5, .strow2 = 5, .stcol2 = 5,
			 .bearing = bearing_of("S") };
    for (int i = 0; i < EZ_SIZE; i++) {
        G.exc[i].row = srow;
	G.exc[i].col = scol;
    }
    airports[0] = G;
    frame_no = 1;
    
    struct plane pl = { .id = 'e', .isjet = true, .target_airport = true,
			.target_num = 0, .start = NULL, .end = NULL,
			.start_tm = -1, .end_tm = -1,
                        .prev = NULL, .next = NULL };
    plstart = plend = &pl;
    struct xyz target = { .row = 5, .col = 5, .alt = 1 };
    int bearing = 0;  // north
    
    struct frame fr = { .opc_start = NULL, .prev = NULL, .next = NULL,
			.n_cand = 0 };
    calc_next_move(&pl, srow, scol, &alt, target, &bearing, false, &fr);
    assert(fr.n_cand == exp_n_cands);

    for (int i = 0; i < fr.n_cand; i++) {
	fprintf(logff, "#%d: bearing %d, alt %d\n", i,
		fr.cand[i].bearing, fr.cand[i].alt);
	assert(fr.cand[i].bearing > 0 || fr.cand[i].alt > 1);
    }
}

static void test_plot_course(bool isprop) {
    // Test a course where have to backtrack.
    /*     0123456789abc
	  0-------------
	  1|...........|
	  2|...........|
	  3|......G....|
	  4|......1....|
	  5|....***1...|
	  6|....*a*2...|
	  7|..***d3**..|
	  8|..*c*2*b*..|
	  9|..***1***..|
	  a|.....1.....|	
	  b|.....S.....|	
	  c-------------	*/

    board_height = 0xD;  board_width = 0xD;
    const int srow = 0xB, scol = 6;
    #define EXC_LEN 10
    struct xyz excr[EXC_LEN] = {
	{ .row = 11, .col = 6, .alt = 0 },
	{ .row = 10, .col = 6, .alt = 1 },
	{ .row = 9, .col = 6, .alt = 1 },
	{ .row = 8, .col = 6, .alt = 2 },
	{ .row = 7, .col = 7, .alt = 3 },
	{ .row = 6, .col = 8, .alt = 2 },
	{ .row = 5, .col = 8, .alt = 1 },
	{ .row = 4, .col = 7, .alt = 1 },
	{ .row = 3, .col = 7, .alt = 1 },
	{ .row = -1, .col = -1, .alt = -2 },
    };
    #define EXC_LEN_B 11
    struct xyz excr2[EXC_LEN_B] = {
	{ .row = 11, .col = 6, .alt = 0 },
	{ .row = 10, .col = 6, .alt = 1 },
	{ .row = 9, .col = 6, .alt = 2 },
	{ .row = 9, .col = 7, .alt = 3 },
	{ .row = 8, .col = 8, .alt = 3 },
	{ .row = 7, .col = 8, .alt = 3 },
	{ .row = 6, .col = 8, .alt = 2 },
	{ .row = 5, .col = 8, .alt = 1 },
	{ .row = 4, .col = 7, .alt = 1 },
	{ .row = 3, .col = 7, .alt = 1 },
	{ .row = -1, .col = -1, .alt = -2 },
    };
    struct course c1 = { .pos = { .row = 6, .col = 6, .alt = 1 },
			 .bearing = -1, .prev = &c1, .next = &c1 };
    struct course c2 = { .pos = { .row = 8, .col = 8, .alt = 1 },
			 .bearing = -1, .prev = &c2, .next = &c2 };
    struct course c3 = { .pos = { .row = 8, .col = 4, .alt = 1 },
			 .bearing = -1, .prev = &c3, .next = &c3 };
    struct course c4 = { .pos = { .row = 7, .col = 6, .alt = 4 },
			 .bearing = -1, .prev = &c4, .next = &c4 };
    struct plane pls[5] = {
      { .id = 'a', .start = &c1, .current = &c1, .end = &c1,
	.prev = NULL, .next = &pls[1] },
      { .id = 'b', .start = &c2, .current = &c2, .end = &c2,
	.prev = &pls[0], .next = &pls[2] },
      { .id = 'c', .start = &c3, .current = &c3, .end = &c3,
	.prev = &pls[1], .next = &pls[4] },
      { .id = 'd', .start = &c4, .current = &c4, .end = &c4,
	.prev = &pls[2], .next = &pls[4] },
      { .id = isprop ? 'S' : 's', .isjet = !isprop, .target_airport = true,
	.target_num = 0, .start = NULL, .current = NULL, .end = NULL,
	.prev = &pls[2], .next = NULL } };
    void add_plane_d() {
	pls[2].next = &pls[3];  pls[4].prev = &pls[3];
    }
    plstart = pls;  plend = &pls[3];
    n_airports = 2;
    struct airport G = { .num = 0, .row = 3, .col = 7,
			 .trow = 3, .tcol = 7, .bearing = bearing_of("N") };
    struct airport S = { .num = 1, .row = srow, .col = scol,
			 .bearing = bearing_of("N") };
    for (int i = 0; i < EZ_SIZE; i++) {
	G.exc[i].row = G.exc[i].col = 0;
    }
    airports[0] = G;
    airports[1] = S;

    int alt = 0;
    frame_no = 1;
    plot_course(&pls[4], srow, scol, alt);
    check_course(pls[4].start, excr, EXC_LEN, isprop);
    remove_course_entries(pls[4].start);
    pls[4].start = pls[4].end = NULL;
    //printf("n_malloc = %d; n_free = %d\n", n_malloc, n_free);
    assert(n_malloc == n_free);

    // Test a double backtrack.
    add_plane_d();
    alt = 0;
    plot_course(&pls[4], srow, scol, alt);
    check_course(pls[4].start, excr2, EXC_LEN_B, isprop);
    remove_course_entries(pls[4].start);
    assert(n_malloc == n_free);
}

static void check_course(struct course *c, struct xyz *excr, int exlen,
			 bool isprop) {
    for (int i = 0; i < exlen; i++) {
	assert(c);
	assert(xyz_eq(c->pos, excr[i]));
	c = c->next;
	if (isprop && i && i != exlen-1) {
	    assert(c);
	    assert(xyz_eq(c->pos, excr[i]));
            c = c->next;
	}
    }
    assert(!c);
}

int testmain() {
    test_calc_next_move();
    test_plot_course(false);
    test_plot_course(true);
    test_excl_landing(1, 9);
    test_excl_landing(2, 14);
    printf("PASS\n");
    return 0;
}


// The call-counting malloc & free stuff
int n_malloc = 0, n_free = 0;

#undef malloc
#undef free
void *count_malloc(const char *msg, size_t s) {
    void *p = malloc(s);
    //fprintf(logff, "%p malloc %s\n", p, msg);
    n_malloc++;
    return p;
}

void count_free(const char *msg, void *p) {
    //fprintf(logff, "%p free %s\n", p, msg);
    n_free++;
    free(p);
}
