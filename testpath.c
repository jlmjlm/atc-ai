#include <stdio.h>
#include <stdbool.h>
#include <assert.h>
#include "atc-ai.h"
#include "pathfind.h"


int board_width = 20;
int board_height = 20;


static void w_blocked(void);


// Verify the behavior of 'calc_next_move':
//    - If headed for something to the NW, but blocked from the W, head
//	N first.  N at alt e > NE at e > e at e > S,SE at e.
//	No W, NW, or SW.

void test_calc_next_move() {
    w_blocked();
}

static void w_blocked() {
    struct plane pl = { .id = 't', .isjet = true, .target_airport = false,
			.bearing_set = true, .target_num = 0,
			.start = NULL, .end = NULL, .start_tm = -1,
			.end_tm = -1, .prev = NULL, .next = NULL };
    int alt = 6;
    struct xyz target = { .row = 0, .col = 0, .alt = 9 };
    int bearing = bearing_of("N");
    struct xy rc = { .row = 5, .col = 5 };
    struct course c1 = { .pos = { .row = 5, .col = 3, .alt = alt },
			 .next = &c1, .prev = &c1 };
    struct op_courses op = { .c = &c1, .prev = NULL, .next = NULL };
    struct frame fr = { .opc_start = &op, .prev = NULL, .next = NULL };
    
    calc_next_move(&pl, rc.row, rc.col, &alt, target, &bearing,
		   false, &fr);

    assert(fr.n_cand == 9);
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

    //XXX: Test being fully blocked.
}

int main() {
    test_calc_next_move();
    printf("PASS\n");
    return 0;
}
