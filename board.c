#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

#include "atc-ai.h"
#include "pathfind.h"

#define MAX_TRIES 10

int board_width, board_height;
bool skip_tick;
int saved_planes = 0;
int info_col;
static const char timestr[] = " Time: ";
static const int timesize = sizeof(timestr)-1;

// For when a plane begins in the top-right corner
static const char alttimestr[] = "7Time: ";

struct exitspec exits[EXIT_MAX];
int n_exits = 0;

struct airport airports[AIRPORT_MAX];
int n_airports = 0;


// The board's dynamic state.
int frame_no = 0;
struct plane *plstart = NULL, *plend = NULL;


static void handle_new_plane(char code, int row, int col, int alt);
static struct plane *get_plane(char code);

static int get_bearing(char code) {
    for (int i = 0; i < 8; i++) {
        if (bearings[i].aircode == code)
            return i;
    }
    return -1;
}

static void new_airport(int row, int col, int bearing, int id) {
    if (n_airports == AIRPORT_MAX) {
        errexit('a', "Too many airports.");
    }
    struct airport *airport = airports + (n_airports++);
    airport->num = id;
    airport->bearing = bearing;
    airport->row = row;
    airport->col = col;
    struct xy xy = apply(row, col, (bearing+4)&7);
    airport->trow = xy.row;
    airport->tcol = xy.col;
    xy = apply(row, col, (bearing+3)&7);
    airport->strow1 = xy.row;
    airport->stcol1 = xy.col;
    xy = apply(row, col, (bearing-3)&7);
    airport->strow2 = xy.row;
    airport->stcol2 = xy.col;
    for (int i = -2; i <= 2; i++) {
        airport->exc[i+2] = apply(row, col, (bearing+i)&7);
    }
    airport->exc[5].row = row;
    airport->exc[5].col = col;
    fprintf(logff, "Found airport #%d at (%d, %d) bearing %s\n",
            id, row, col, bearings[bearing].longname);
}

static void find_airports() {
    int r, c;
    for (r = 1; r < board_height-1; r++) {
        for (c = 1; c < board_width-1; c++) {
            if (isdigit(D(r, 2*c+1)) && D(r, 2*c) != '*' &&
                    (D(r, 2*c) == 'v' || !isalpha(D(r, 2*c)))) {
                fprintf(logff, "Found '%c%c' at (%d, %d)\n",
                        D(r, 2*c), D(r, 2*c+1), r, c);
                int bearing = get_bearing(D(r, 2*c));
                new_airport(r, c, bearing, D(r, 2*c+1) - '0');
            }
        }
    }
}

static int get_frame_no() {
    int fnum;
    if (memcmp(display + info_col - 1, timestr, timesize) &&
            memcmp(display + info_col - 1, alttimestr, timesize)) {
        errexit('t', "Can't find frame number.  Got '%.*s' instead of '%.*s'",
                timesize, display + info_col - 1, timesize, timestr);
    }
    int rv = sscanf(display+(info_col-1)+timesize, "%d", &fnum);
    if (rv != 1) {
        errexit('t', "Can't read frame number.");
    }
    return fnum;
}

static inline bool verify_mark() {
    const char *exm = mark_sense ? "z: mark" : "z: unmark";
    fprintf(logff, "Checking mark: expected \"%s\", actual \"%.*s\"\n",
            exm, strlen(exm), &D(board_height, 0));   //XXX
    return !memcmp(exm, &D(board_height, 0), strlen(exm));
}

static inline const char *pmin(const char *a, const char *b) {
    return (a < b) ? a : b;
}

static bool board_init() {
    const char *tee = memchr(display, 'T', screen_width);
    if (tee == NULL) {
        if (frame_no == 0) {
            static int n_tries = 0;
            fprintf(logff, "Failed to init board, try #%d\n", ++n_tries);
            if (n_tries < MAX_TRIES)
                return false;
        }
        errexit(' ', "Can't determine board width.");
    }
    info_col = tee - display;
    const char *spc = memchr(display, ' ', screen_width);
    if (spc[-1] == '7' && isalpha(spc[-2]))
        spc--;
    board_width = pmin(spc, tee-1) - display;
    if (board_width % 2 == 0) {
        errexit(2, "Invalid width of %d.5 chars.", board_width/2);
    }
    board_width = (board_width + 1) / 2;
    if (board_width <= 5) {
        errexit(board_width+1, "Board unreasonably thin.");
    }
    if (board_width > 80) {
        errexit(board_width, "Board unreasonably wide.");
    }
    for (int i = 1; i < screen_height; i++) {
        if (D(i, 0) == ' ') {
            board_height = i;
            break;
        }
    }
    if (board_height == 0) {
        errexit(' ', "Can't determine board height.");
    }
    if (board_height <= 5) {
        errexit(board_height+1, "Board unreasonably short.");
    }
    if (board_height > 50) {
        errexit(board_height, "Board unreasonably tall.");
    }
    if (D(board_height-1, 0) != '-' && D(board_height-1, 2) != '-') {
        errexit('L', "Can't find lower left corner of board.");
    }
    int fnum = get_frame_no();
    if (fnum != 1) {
        errexit('t', "Starting at frame %d instead of 1.", fnum);
    }

    find_airports();
    fprintf(logff, "Board is %d by %d and the info column is %d.\n",
        board_width, board_height, info_col);

    return true;
}

struct airport *get_airport(int n) {
    for (int i = 0; i < n_airports; i++) {
        if (airports[i].num == n)
            return &airports[i];
    }
    return NULL;
}

struct exitspec *get_exit(int n) {
    for (int i = 0; i < n_exits; i++) {
        if (exits[i].num == n)
            return &exits[i];
    }
    return NULL;
}

static void new_exit(int row, int col) {
    int exit_num = D(row, 2*col) - '0';
    struct exitspec *spec = get_exit(exit_num);
    if (spec && (spec->row != row || spec->col != col)) {
        errexit('c', "conflict: exit %d found at both (%d, %d) and (%d, %d)",
                exit_num, spec->row, spec->col, row, col);
    }
    if (spec)
        return;
    spec = &exits[n_exits++];
    if (n_exits > EXIT_MAX) {
        errexit('e', "Too many exits found.");
    }
    spec->num = exit_num; spec->row = row; spec->col = col;
    fprintf(logff, "Found exit %d at (%d, %d)\n", exit_num, row, col);
}

static void check_for_exits() {
    // Exits on N and S borders are of the form "#-"
    int i;
    for (i = 0; i < board_width-1; i++) {
        if (isdigit(D(0, i*2)) && D(0, i*2+1) == '-')
            new_exit(0, i);
        if (isdigit(D(board_height-1, i*2)) && D(board_height-1, i*2+1) == '-')
            new_exit(board_height-1, i);
    }

    // Exits on E and W borders are of the form "# "
    for (i = 0; i < board_height; i++) {
        if (isdigit(D(i, 0)) && D(i, 1) == ' ')
            new_exit(i, 0);
        if (isdigit(D(i, 2*board_width-2)) && D(i, 2*board_width-1) == ' ')
            new_exit(i, board_width-1);
        if (i == 0 || i == board_height-1)
            fprintf(logff, "D[%d, %d..%d] = '%c%c'\n", i, 2*board_width-2,
                    2*board_width-1, D(i, 2*board_width-2),
                    D(i, 2*board_width-1));
    }
}

struct course *free_course_entry(struct course *ci) {
    struct course *rv = ci->next;
    assert(ci->prev == NULL);
    if (ci->next)
        ci->next->prev = ci->prev;
    free(ci);
    return rv;
}

void remove_course_entries(struct course *c) {
    while (c)
        c = free_course_entry(c);
}

static struct plane *remove_plane(struct plane *p) {
    struct plane *rv = p->next;
    remove_course_entries(p->start);
    if (p->prev)
        p->prev->next = p->next;
    else {
        assert(plstart == p);
        plstart = p->next;
    }
    if (p->next)
        p->next->prev = p->prev;
    else {
        assert(plend == p);
        plend = p->prev;
    }
    free(p);
    saved_planes++;
    return rv;
}

static bool southbound_airport(char code, int alt, int row, int col) {
    if (code == 'v') {
        const struct airport *ap = get_airport(alt);
        return ap && bearings[ap->bearing].degree == 180 &&
                ap->row == row && ap->col == col;
    }

    return false;
}

static void verify_planes() {
    for (struct plane *i = plstart; i; ) {
        assert(i->current_tm == frame_no);
        struct course *next = i->current->next;
        if (!next) {
            assert(i->end_tm == frame_no);
            i = remove_plane(i);
            continue;
        }
        if (next->pos.alt == -2)
            land_at_airport(i->id, i->target_num);
        else {
            if (i->current->bearing != next->bearing)
                order_new_bearing(i->id, next->bearing);
            if (i->current->pos.alt != next->pos.alt)
                order_new_altitude(i->id, next->pos.alt);
        }

        char code = D(i->current->pos.row, i->current->pos.col*2);
        char alt = D(i->current->pos.row, i->current->pos.col*2+1);
        if (i->current->pos.alt == 0) {
            if (!isdigit(alt) || (!isalpha(code) && !memchr("<>^v", code, 4))) {
                fprintf(logff, "[Tick %d] Expected to find plane '%c' at "
                               "(%d, %d) but instead found '%c%c'\n",
                        frame_no, i->id,
                        i->current->pos.row, i->current->pos.col, code, alt);
                errexit('p', "Found '%c%c' where expected to find a "
                             "plane or airport.", code, alt);
            }
        } else if (!isalpha(code) || !isdigit(alt)) {
            fprintf(logff, "[Tick %d] Expected to find plane '%c' at "
                           "(%d, %d) but instead found '%c%c'\n",
                    frame_no, i->id, i->current->pos.row, i->current->pos.col,
                    code, alt);
            errexit('p', "Found '%c%c' where expected to find a plane.",
                    code, alt);
        }
        if (code == i->id && alt-'0' != i->current->pos.alt &&
                !southbound_airport(code, alt-'0',
                                    i->current->pos.row, i->current->pos.col)) {
            fprintf(logff, "Found plane '%c' at altitude %c=%d where "
                           "expected to find it at altitude %d\n",
                    code, alt, alt-'0', i->current->pos.alt);
            errexit('a', "Found plane %c at altitude %c=%d where "
                         "expected to find it at altitude %d.",
                    code, alt, alt-'0', i->current->pos.alt);
        }
        i = i->next;
    }
}

static const char pls1[] = " pl dt  comm ";
static const char pls2[] = "7pl dt  comm ";

static void check_pldt() {
    char *base = &D(2, info_col-1);
    if (memcmp(base, pls1, sizeof(pls1)-1) &&
            memcmp(base, pls2, sizeof(pls2)-1)) {
        errexit('P', "Expected \"%s\" but found \"%.*s\"",
                pls1, sizeof(pls1)-1, base);
    }
}

static inline bool plane_at_airport(char id) {
    struct plane *p = get_plane(id);
    return p->id == id && p->current && p->current->pos.alt == 0;
}

static void handle_airport_plane(char id, int dtpos) {
    static const char holding[] = ": Holding @ A";
    static const int holdlen = sizeof(holding)-1;
    assert(!memcmp(&D(dtpos, info_col + 5), holding, holdlen));
    char apnum = D(dtpos, info_col + 5 + holdlen);
    struct airport *ap = get_airport(apnum-'0');
    assert(ap);
    handle_new_plane(id, ap->row, ap->col, 0);
}

static void new_airport_planes() {
    check_pldt();
    for (int i = 3; i < screen_height && D(i, info_col) != '*'; i++) {
        char id = D(i, info_col);
        if (!isalpha(id))
            continue;
        if (D(i, info_col+1) != '0')
            continue;
        if (get_plane(id) == NULL) {
            handle_airport_plane(id, i);
        } else {
            assert(plane_at_airport(id));
        }
    }
}

static void target(struct plane *p) {
    char id = p->id;
    for (int i = 3; i < screen_height && D(i, info_col) != '*'; i++) {
        if (D(i, info_col) != id)
            continue;
        char ttype = D(i, info_col+3);
        char tnum = D(i, info_col+4);
        assert(D(i, info_col+5) == ':');
        assert(isdigit(tnum) && (ttype == 'A' || ttype == 'E'));
        p->target_airport = (ttype == 'A');
        p->target_num = tnum-'0';
        return;
    }

    errexit('T', "Unable to find the target of plane %c", id);
}

static struct plane *get_plane(char code) {
    for (struct plane *i = plstart; i; i = i->next) {
        if (i->id == code)
            return i;
    }

    return NULL;
}

static void handle_found_plane(char code, int alt, int row, int col) {
    // It's OK if this "plane" is actually a 'v' airport.
    if (southbound_airport(code, alt, row, col))
        return;

    // See if the plane's ID matches any existing ones.
    struct plane *p = get_plane(code);

    if (p) {
        struct xyz pos = p->current->pos;
        if (pos.alt != alt || pos.row != row || pos.col != col) {
            fprintf(logff, "[Tick %d] Expected to find plane '%c' at "
                           "(%d, %d, %d) but actually at (%d, %d, %d)\n",
                    frame_no, code, pos.row, pos.col, pos.alt, row, col, alt);
            errexit('E', "Expected to find plane %c at (%d, %d, %d) but "
                         "actually at (%d, %d, %d).",
                    code, pos.row, pos.col, pos.alt, row, col, alt);
        }
        // Plane's position is A-OK.
        return;
    }

    // It's a new plane.
    if (alt != 7) {
        errexit('7', "New plane %c found at flight level %d != 7.", code, alt);
    }
    handle_new_plane(code, row, col, alt);
}

static void handle_new_plane(char code, int row, int col, int alt) {
    struct plane *p = malloc(sizeof(*p));
    p->id = code;
    p->isjet = islower(code);
    target(p);
    plot_course(p, row, col, alt);
    if (p->start) {
        assert(p->start->pos.alt == alt);
        assert(p->start->pos.row == row);
        assert(p->start->pos.col == col);
        struct course *next = p->start->next;
        if (next) {
            if (alt)
                order_new_bearing(p->id, next->bearing);
            if (next->pos.alt != alt)
                order_new_altitude(p->id, next->pos.alt);
        }
        p->current = p->start;
        p->current_tm = p->start_tm;
    }

    p->next = NULL;
    p->prev = plend;
    if (plend) {
        plend->next = p;
    } else {
        plstart = p;
    }
    plend = p;
}

static void find_new_planes() {
    int r, c;
    for (r = 0; r < board_height; r++) {
        for (c = 0; c < board_width; c++) {
            char code = D(r, 2*c);
            char alt = D(r, 2*c+1);
            if (!isalpha(code))
                continue;
            if (!isdigit(alt)) {
                errexit('A', "\"Plane\" '%c%c' has bad altitude.", code, alt);
            }
            handle_found_plane(code, alt-'0', r, c);
        }
    }
}

static void update_plane_courses() {
    for (struct plane *p = plstart; p; p = p->next) {
        p->current = p->current->next;
        p->current_tm++;
    }
}

bool update_board() {
    if (frame_no == 0 && !mark_sent) {
        if (!board_init())
            return false;  // Board not set-up yet.  Try again.
    }

    int new_frame_no = get_frame_no();
    if (new_frame_no == frame_no) {
        // Clock hasn't ticked, no-op.
        return false;
    }

    if (new_frame_no != frame_no+1) {
        errexit('f', "Frame number jumped from %d to %d.", frame_no,
                new_frame_no);
    }

    if (!mark_sent) {
        mark_msg();
        return false;
    }

    if (!verify_mark())
        return false;

    de_mark_msg();

    if (frame_no <= 3)
        check_for_exits();

    frame_no = new_frame_no;
    verify_planes();
    find_new_planes();
    new_airport_planes();
    update_plane_courses();
    if (skip_tick) {
        next_tick();
        mark_msg();
    }

    if (frame_no % 1024u == 0) {
        fprintf(logff, "n_malloc = %d; n_free = %d; difference = %d\n",
                n_malloc, n_free, n_malloc - n_free);
    }

    return true;
}
