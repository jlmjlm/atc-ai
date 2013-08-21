#include <stdio.h>
#include "atc-ai.h"


void order_new_bearing(char id, int bearing) {
    fprintf(outf, "%ct%c\n", id, bearings[bearing].key);
}

void order_new_altitude(char id, int alt) {
    fprintf(outf, "%ca%d\n", id, alt);
}

void land_at_airport(char id, int airport_num) {
    fprintf(outf, "%ctta%d\n%ca0\n", id, airport_num, id);
}

void next_tick() {
    putc('\n', outf);
}
