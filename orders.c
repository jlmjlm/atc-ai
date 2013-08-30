#include <stdio.h>
#include <unistd.h>
#include "atc-ai.h"


int typing_delay_ms = 0;


static void delay() {
    if (typing_delay_ms)
	usleep(typing_delay_ms * 1000);
}

void order_new_bearing(char id, int bearing) {
    fprintf(outf, "%ct%c\n", id, bearings[bearing].key);
    delay();
}

void order_new_altitude(char id, int alt) {
    fprintf(outf, "%ca%d\n", id, alt);
    delay();
}

void land_at_airport(char id, int airport_num) {
    fprintf(outf, "%ctta%d\n%ca0\n", id, airport_num, id);
    delay();
}

void next_tick() {
    putc('\n', outf);
}
