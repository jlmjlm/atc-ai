// Copyright 2013 Jacob L. Mandelson
// This code may be distributed under the terms of the Affero General
// Public License, ver. 3.  See the file "AGPLv3" for details.

#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <assert.h>
#include "atc-ai.h"
#include "pathfind.h"


const struct bearing bearings__[9] = {
    { -1, 0, 0, '\0', '\0', "O", "stationary" },
    { 0, -1, 0, 'w', '^', "N", "north" },
    { 45, -1, 1, 'e', '\0', "NE", "northeast" },
    { 90, 0, 1, 'd', '>', "E", "east" },
    { 135, 1, 1, 'c', '\0', "SE", "southeast" },
    { 180, 1, 0, 'x', 'v', "S", "south" },
    { 225, 1, -1, 'z', '\0', "SW", "southwest" },
    { 270, 0, -1, 'a', '<', "W", "west" },
    { 315, -1, -1, 'q', '\0', "NW", "northwest" },
};
const struct bearing *const bearings = bearings__ + 1;


static void free_op_courses(struct op_courses *oc);

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

static bool is_exit_at(int row, int col) {
    for (int i = 0; i < n_exits; i++) {
        if (exits[i].row == row && exits[i].col == col)
            return true;
    }

    return false;
}

static int calc_bearing(int row, int col) {
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

    for (int i = 0; i < 8; i++) {
        if (is_exit_at(row-bearings[i].drow, col-bearings[i].dcol))
            return i;
    }

    // Getting desperate, so make a guess based on proximity to boundary.
    if (!is_exit_at(row, col)) {
        fprintf(logff, "Caution: New plane at (%d, %d) isn't at exit or near "
                       "exit.\n", row, col);
    }
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
                            int bearing, bool cleared_exit, int trace_tick) {
    if (trace_tick) {
        fprintf(logff, "\t%d: (%d, %d, %d)@%d\n", trace_tick, row, col, alt,
                bearings[bearing].degree);
    }
    struct course *nc = malloc(sizeof(*nc));
    nc->pos.row = row;  nc->pos.col = col;  nc->pos.alt = alt;
    nc->bearing = bearing;
    nc->cleared_exit = cleared_exit;
    nc->at_exit = false;
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


// Euclidian distance.  Maybe taxicab distance would be better?
// Or max(dr, dc, da)?
static int edist(int r1, int c1, int a1, int r2, int c2, int a2) {
    int dr = r1 - r2;
    int dc = c1 - c2;
    int da = a1 - a2;
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

static int cdist(int r, int c, int alt, struct xyz target,
                 const struct plane *p, int srow, int scol) {
    struct xy sxy = { .row = srow, .col = scol };
    if (p->target_airport && in_airport_excl(sxy, 1, p->target_num)) {
        // Use an airport's secondary targets.
        const struct airport *ap = get_airport(p->target_num);
        int dist1 = edist(r, c, alt, ap->strow1, ap->stcol1, 2);
        int dist2 = edist(r, c, alt, ap->strow2, ap->stcol2, 2);
        return (dist1 < dist2) ? dist1 : dist2;
    }

    return edist(r, c, alt, target.row, target.col, target.alt);
}

static bool pos_adjacent(struct xyz pos, struct xy rc, int alt) {
    if (pos.alt == 0)
        return false;

    return abs(pos.row - rc.row) < 2 && abs(pos.col - rc.col) < 2 &&
           abs(pos.alt - alt) < 2;
}

struct blp { int bearing, alt; bool isjet; };

static struct blp adjacent_another_plane(struct xy rc, int alt, bool imjet,
                                         const struct op_courses *opc) {
    for ( ; opc; opc = opc->next) {
        if (!opc->c || opc->c->at_exit)
            continue;
        struct xyz pos = opc->c->pos;
        if (pos_adjacent(pos, rc, alt)) {
            struct blp rv = { opc->c->bearing, pos.alt, opc->isjet };
            return rv;
        }
        if (!imjet && opc->c->next) {
            pos = opc->c->next->pos;
            if (pos_adjacent(pos, rc, alt)) {
                struct blp rv = { opc->c->next->bearing, pos.alt, opc->isjet };
                return rv;
            }
        }
    }

    struct blp rv = { -1, -1, true };
    return rv;
}

// Also have a penalty for a jet aligning with a prop at the
// same FL and orthodistance 2, because that's essentially
// "matching" up courses (to be tighter with this check,
// we should check bearings).
// FIXME: self-test this
static bool planes_aligned(struct xy rc, int alt, bool imjet,
                           const struct op_courses *opc) {
    if (!imjet)
        return false;
    for ( ; opc; opc = opc->next) {
        if (!opc->c || opc->c->at_exit || opc->isjet)
            continue;
        struct xyz pos = opc->c->pos;
        int dr = rc.row - pos.row;
        int dc = rc.col - pos.col;
        if (alt == pos.alt && ((dr == 0 && abs(dc) == 2) ||
                               (dc == 0 && abs(dr) == 2)))
            return true;
    }
    return false;
}

static void new_cand(struct frame *frame, int bearing, int alt, int dist) {
    int i = frame->n_cand++;
    frame->cand[i].bearing = bearing;
    frame->cand[i].alt = alt;
    frame->cand[i].distance = dist;
}

#define MATCHCOURSE_PENALTY 1000
#define CHANGEALT_BONUS 100
#define BLP_MAX 10

static void add_blocking_plane(struct blp *blocking_planes, int *n_blp,
                               struct blp adjacent_plane) {
    for (int i = 0; i < *n_blp; i++) {
        if (blocking_planes[i].bearing == adjacent_plane.bearing &&
                blocking_planes[i].alt == adjacent_plane.alt)
            return;
    }

    if (*n_blp == BLP_MAX)
        fprintf(logff, "Warning: Too many blocking planes.\n");
    else
        blocking_planes[(*n_blp)++] = adjacent_plane;
}

__attribute__((format(printf, 2, 3) ))
static inline void tracelog(const bool trace, const char *fmt, ...) {
    if (trace) {
        va_list va;
        va_start(va, fmt);
        vfprintf(logff, fmt, va);
        va_end(va);
    }
}

void calc_next_move(const struct plane *p, const int srow, const int scol,
                    int *alt, const struct xyz target, int *bearing,
                    const bool cleared_exit, struct frame *frame) {
    // Avoid obstacles.  Obstacles are:  The boundary except for the
    // target exit at alt==9, adjacency with another plane (props have
    // to check this at t+1 and t+2), within 2 of an exit at alt 6-8 if
    // it's cleared the exit, the exclusion area of an airport at alt <= 2.

    // Incur a penalty for matching the bearing/altitude of a
    // blocking airplane (because it'll just continue to block).
    struct blp blocking_planes[BLP_MAX];
    int n_blp = 0;

    int nalt;

    const bool trace = (p->id == 'j' && srow == 1 && scol == 9 && *alt == 9 &&
                  bearings[*bearing].aircode == '<' &&
                  target.row == 0 && target.col == 29 && target.alt == 9);

    // If the plane's at the airport, it can only hold or take off.
    if (*alt == 0) {
        struct xy rc = apply(srow, scol, *bearing);
        frame->cand[0].bearing = frame->cand[1].bearing = *bearing;
        frame->cand[0].alt = 0;  frame->cand[1].alt = 1;
        if (adjacent_another_plane(rc, 1, p->isjet, frame->opc_start).alt > 0) {
            // Can't take off, can only hold.
            frame->n_cand = 1;
        } else {
            *alt = 1;
            frame->n_cand = 2;
        }
        return;
    }

    frame->n_cand = 0;
    for (int turn = -2; turn <= 2; turn++) {
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
            if (target.alt == 9 && nalt == 9 &&
                    rc.row == target.row && rc.col == target.col) {
                // Reached the proper exit gate.  Can't collide here,
                // planes just immediately disappear.
                new_cand(frame, nb, nalt, -10*MATCHCOURSE_PENALTY);
                break;
            }
            if (on_boundary)    // ... and not at the target exit
                continue;
            if (cleared_exit && p->target_airport &&
                    in_airport_excl(rc, nalt, p->target_num))
                continue;
            if (nalt == 1 && p->target_airport &&
                    rc.row == target.row && rc.col == target.col &&
                    in_airport_excl(apply(srow, scol, -1), *alt, p->target_num))
                continue;
            struct blp adjacent_plane =
                adjacent_another_plane(rc, nalt, p->isjet, frame->opc_start);

            if (adjacent_plane.alt > 0) {
                add_blocking_plane(blocking_planes, &n_blp, adjacent_plane);
                tracelog(trace, "Candidate move to (%d, %d, %d) bearing %s "
                                "blocked by %s plane at altitude %d "
                                "bearing %s\n",
                         rc.row, rc.col, nalt, bearings[nb].shortname,
                         adjacent_plane.isjet ? "jet" : "prop",
                         adjacent_plane.alt,
                         bearings[adjacent_plane.bearing].shortname);
                continue;
            }
            if (cleared_exit && (rc.row <= 2 || rc.row >= board_height - 3 ||
                                 rc.col <= 2 || rc.col >= board_width - 3) &&
                    ((p->target_airport && nalt >= 6) ||
                     (!p->target_airport && nalt != 9)))
                continue;
            bool aligned = planes_aligned(rc, nalt, p->isjet, frame->opc_start);
            int penalty = aligned ? MATCHCOURSE_PENALTY/2 : 0;
            int distance = penalty + cdist(rc.row, rc.col, nalt, target, p,
                                           srow, scol);
            new_cand(frame, nb, nalt, distance);
            tracelog(trace, "Adding candidate move to (%d, %d, %d) bearing %s "
                            "distance=%d\n", rc.row, rc.col, nalt,
                     bearings[nb].longname, distance);
        }
    }
    assert(frame->n_cand <= 15);
    if (frame->n_cand == 0) {
        tracelog(trace, "Warning: Can't find safe path for plane '%c'\n",
                 p->id);
        *alt = -1;
        return;
    }
    tracelog(trace, "Checking %d candidate moves against %d blocking planes.\n",
             frame->n_cand, n_blp);
    for (int i = 0; i < frame->n_cand; i++) {
        for (int j = 0; j < n_blp; j++) {
             if (frame->cand[i].bearing != blocking_planes[j].bearing ||
                    p->isjet != blocking_planes[j].isjet) {
                tracelog(trace, "Not applying matchcourse penalty: "
                                "%s c_bearing %s vs. %s b_bearing %s "
                                "(%d/%d vs. %d/%d)\n",
                         p->isjet ? "jet" : "prop",
                         bearings[frame->cand[i].bearing].shortname,
                         blocking_planes[j].isjet ? "jet" : "prop",
                         bearings[blocking_planes[j].bearing].shortname,
                         frame->cand[i].bearing, p->isjet,
                         blocking_planes[j].bearing, blocking_planes[j].isjet);
                continue;
             }
             int da = abs(frame->cand[i].alt - blocking_planes[j].alt);
             if (da == 0) {
                tracelog(trace, "Applying matchcourse penalty to plane %c "
                                "bearing %s.\n",
                         p->id, bearings[blocking_planes[j].bearing].longname);
                frame->cand[i].distance += MATCHCOURSE_PENALTY;
             } else if (da == 1) {
                tracelog(trace, "Applying minor matchcourse penalty to "
                                "plane %c bearing %s.\n",
                         p->id, bearings[blocking_planes[j].bearing].longname);
                frame->cand[i].distance += MATCHCOURSE_PENALTY/10;
             } else {
                tracelog(trace, "Not applying matchcourse penalty: "
                                "c_alt %d vs. b_alt %d\n",
                         frame->cand[i].alt, blocking_planes[j].alt);
             }
        }
    }
    qsort(frame->cand, frame->n_cand, sizeof(*frame->cand), distcmp);

    int old_dist = cdist(srow, scol, *alt, target, p, srow, scol);
    if (frame->cand[frame->n_cand-1].distance > old_dist) {
        // We're being pushed away from the destination.  Apply the
        // alt change bonus and re-sort.
        for (int i = 0; i < frame->n_cand; i++) {
            for (int j = 0; j < n_blp; j++) {
                if (*alt == blocking_planes[j].alt &&
                        frame->cand[i].alt != blocking_planes[j].alt)
                    frame->cand[i].distance -= CHANGEALT_BONUS;
            }
        }
        qsort(frame->cand, frame->n_cand, sizeof(*frame->cand), distcmp);
    }

    *bearing = frame->cand[frame->n_cand-1].bearing;
    *alt = frame->cand[frame->n_cand-1].alt;
}

static void new_op_course(const struct course *c,
                          struct op_courses **st,
                          struct op_courses **end,
                          bool isjet) {
    struct op_courses *ne = malloc(sizeof(*ne));
    ne->c = c;
    ne->isjet = isjet;
    ne->prev = *end;
    ne->next = NULL;
    if (*end)
        (**end).next = ne;
    else
        *st = ne;
    *end = ne;
}

static struct op_courses *next_opc(const struct op_courses *o) {
    struct op_courses *st = NULL, *end = NULL;
    while (o) {
        new_op_course(o->c ? o->c->next : NULL, &st, &end, o->isjet);
        o = o->next;
    }
    return st;
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

static struct xyz backtrack(int *tick, bool *cleared_exit,
                            struct course **cend,
                            struct frame **lfrend) {
    --*tick;
    struct course *prev = (*cend)->prev;
    if (prev == NULL) {
        struct xyz pos = (*cend)->pos;
        errexit('x', "Aieee.  Plane at (%d, %d, %d) is impossible.",
                pos.row, pos.col, pos.alt);
    }
    struct xyz rv = prev->pos;
    *cleared_exit = prev->cleared_exit;
    free(*cend);
    *cend = prev;
    prev->next = NULL;

    struct frame *fr = *lfrend;
    *lfrend = fr->prev;
    (*lfrend)->next = NULL;
    free_op_courses(fr->opc_start);
    free(fr);

    return rv;
}


static void free_op_courses(struct op_courses *oc) {
    while (oc) {
        struct op_courses *next = oc->next;
        free(oc);
        oc = next;
    }
}

static void free_framelist(struct frame *fp) {
    while (fp) {
        struct frame *next = fp->next;

        free_op_courses(fp->opc_start);
        free(fp);
        fp = next;
    }
}

static void log_course(const struct plane *p) {
    fprintf(logff, "Plotting plane %c's course from %d:(%d, %d, %d) to "
                   "%d:(%d, %d, %d)\n", p->id, p->start_tm,
            p->start->pos.row, p->start->pos.col, p->start->pos.alt,
            p->end_tm, p->end->pos.row, p->end->pos.col, p->end->pos.alt);
    int tick = p->start_tm;
    struct course *c = p->start;
    for ( ; c; tick++, c = c->next) {
        fprintf(logff, "\t%c %d: (%d, %d, %d) bearing %s\n", p->id, tick,
                c->pos.row, c->pos.col, c->pos.alt,
                bearings[c->bearing].shortname);
        if (c->bearing < 0)
            break;
    }
}

static void log_all_courses() {
    for (struct plane *p = plstart; p; p = p->next)
        log_course(p);
}

static void make_new_fr(struct frame **endp);

// The "record" longest course planes of type jet & prop.
struct record { int steps, moves; };
static struct record rec_jet, rec_prop;  // static init. == zeros


void plot_course(struct plane *p, int row, int col, int alt) {
    const bool trace = (p->id == 'i' && frame_no == 575);

    struct frame *frstart = malloc(sizeof *frstart);
    struct frame *frend = frstart;
    frstart->prev = frstart->next = NULL;
    frstart->opc_start = NULL;
    struct op_courses *opc_end = NULL;

    assert(alt == 7 || alt == 0);
    int bearing = alt ? calc_bearing(row, col)
                      : get_airport_xy(row, col)->bearing;
    bool cleared_exit = false;
    struct xyz target;

    for (struct plane *pi = plstart; pi; pi = pi->next) {
        if (pi == p)
            continue;
        new_op_course(pi->current, &frstart->opc_start, &opc_end, pi->isjet);
    }
    incr_opc(frstart->opc_start);

    if (p->target_airport) {
        struct airport *a = get_airport(p->target_num);
        if (a == NULL) {
            errexit('u', "Plane '%c' headed to unknown airport %d.",
                    p->id, p->target_num);
        }
        target.alt = 1;
        target.row = a->trow;
        target.col = a->tcol;
    } else {
        struct exitspec *e = get_exit(p->target_num);
        if (e == NULL) {
            errexit('u', "Plane '%c' headed to unknown exit %d.",
                    p->id, p->target_num);
        }
        target.alt = 9;
        target.row = e->row;
        target.col = e->col;
    }
    tracelog(trace, "Tracing plane %c's course from %d:(%d, %d, %d)@%d to "
                    "(%d, %d, %d)\n",
             p->id, frame_no, row, col, alt, bearings[bearing].degree,
             target.row, target.col, target.alt);

    p->start = p->current = p->end = NULL;
    add_course_elem(p, row, col, alt, bearing, false, trace ? frame_no : 0);
    p->start_tm = p->current_tm = frame_no;
    int tick = frame_no+1;
    int steps = 0, moves = 0;

    /* Operation of the "plotting course" machine:
     *    (A) Get a frame for the current pos'n.
     *    (B) If frame has cands, step ahead to the best cand and return to (A).
     *    (C) If not, step back to parent frame and remove the cand and
     *        return to (B).
     */
    for (;;) {
        if (++steps > 200) {
            log_course(p);
            errexit('8', "Plane %c stuck in an infinite loop.", p->id);
        }

        // Plane doesn't move if it's a prop and the tick is odd...
        // ...except that a prop plane in an exit will pop out of it.
        if (!p->isjet && tick%2 == 1 && row != 0 && col != 0 &&
                row != board_height-1 && col != board_width-1) {
            add_course_elem(p, row, col, alt, bearing, cleared_exit,
                            trace ? tick : 0);
            tick++;
            frend->n_cand = -3;
            make_new_fr(&frend);
            continue;
        }

        moves++;
        calc_next_move(p, row, col, &alt, target, &bearing, cleared_exit,
                       frend);
        assert((alt < 0) == (frend->n_cand <= 0));
        while (frend->n_cand <= 0) {
            tracelog(trace, "Backtracking at step %d move %d tick %d\n",
                     steps, moves, tick);
            struct xyz bt_pos = backtrack(&tick, &cleared_exit, &p->end,
                                          &frend);
            moves--;

            // Check for a prop. plane's non-move.
            // TODO: Do we have to worry about the "pop out of an exit" move
            // that props get at their first tick?
            if (frend->n_cand == -3) {
                tracelog(trace,
                         "Backtracking over prop's non-move at tick %d\n",
                         tick);
                assert(!p->isjet);
                bt_pos = backtrack(&tick, &cleared_exit, &p->end, &frend);
                assert(frend->n_cand != -3);
            }

            row = bt_pos.row;  col = bt_pos.col;
            tracelog(trace, "After backtracking:  %d: pos(%d, %d, %d) and %d "
                            "remaining candidates\n", tick, bt_pos.row,
                     bt_pos.col, bt_pos.alt, frend->n_cand - 1);

            if (--frend->n_cand > 0) {
                // We've found a new candidate that's available after
                // backtracking, so stop backtracing and get on with it.
                alt = frend->cand[frend->n_cand-1].alt;
                bearing = frend->cand[frend->n_cand-1].bearing;
                break;
            }
            tracelog(trace, "No new candidates found at tick %d.  Backtracking "
                            "again.\n", tick);
        }
        if (alt) {
            row += bearings[bearing].drow;
            col += bearings[bearing].dcol;
        }

        add_course_elem(p, row, col, alt, bearing, cleared_exit,
                        trace ? tick : 0);
        tick++;

        if (row == target.row && col == target.col && alt == target.alt) {
            // We've reached the target.  Clean-up and return.
            if (p->target_airport) {
                if (!p->isjet) {
                    add_course_elem(p, row, col, alt, bearing, cleared_exit,
                                    trace ? tick : 0);
                    tick++;
                }
                add_course_elem(p, -1, -1, -2, -1, cleared_exit,
                                trace ? tick : 0);
                p->end_tm = tick;
            } else {
                // For an exit, the plane disappears at reaching it.
                p->end_tm = tick-1;
                p->end->at_exit = true;
            }

            free_framelist(frstart);

            if (!quiet) {
                struct record *rec = p->isjet ? &rec_jet : &rec_prop;
                if (steps > rec->steps || moves > rec->moves) {
                    if (steps > rec->steps)
                        rec->steps = steps;
                    if (moves > rec->moves)
                        rec->moves = moves;
                    fprintf(logff, "New record long route: plane '%c' at time "
                                   "%d in %d steps/%d moves.\n",
                            p->id, frame_no, steps, moves);
                    log_course(p);
                    log_all_courses();
                }
            }

            return;
        }

        if (!cleared_exit && alt > 1 && ((row > 2 && row < board_height-3 &&
                col > 2 && col < board_width-3) || alt < 6 || alt == 9)) {
            cleared_exit = true;
        }

        make_new_fr(&frend);
    }
}

static void make_new_fr(struct frame **endp) {
    struct frame *newfr = malloc(sizeof *newfr);
    newfr->opc_start = next_opc((*endp)->opc_start);
    newfr->prev = *endp;
    newfr->next = NULL;
    assert((*endp)->next == NULL);
    (*endp)->next = newfr;
    *endp = newfr;
}
