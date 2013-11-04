// Copyright 2013 Jacob L. Mandelson
// This code may be distributed under the terms of the Affero General
// Public License, ver. 3.  See the file "AGPLv3" for details.

struct step { int bearing, alt, distance; };

struct op_courses {
    const struct course *c;
    bool isjet;
    struct op_courses *prev, *next;
};

struct frame {
    int n_cand;
    struct step cand[15];
    struct op_courses *opc_start;
    struct frame *prev, *next;
};


// 'extern' for testing
extern void calc_next_move(const struct plane *p, int srow, int scol, int *alt,
                           struct xyz target, int *bearing, bool cleared_exit,
                           struct frame *frame);
extern void remove_course_entries(struct course *c);
