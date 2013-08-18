struct step { int bearing, alt, distance; };

struct op_courses {
    const struct course *c;
    struct op_courses *prev, *next;
};

struct frame {
    int n_cand;
    struct step cand[15];
    struct op_courses *opc_start;
    struct frame *prev, *next;
};


// Testing functions
extern void test_calc_next_move(void);
extern void calc_next_move(struct plane *p, int srow, int scol, int *alt,
                           struct xyz target, int *bearing, bool cleared_exit,
                           struct frame *frame);

