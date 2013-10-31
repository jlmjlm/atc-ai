#include <unistd.h>
#include "atc-ai.h"


int tqhead = 0, tqtail = 0;
char tqueue[TQ_SIZE];

static void queue_string(const char s[]) {
    while (*s) {
        tqueue[tqtail] = *s;
        tqtail = (tqtail+1)%TQ_SIZE;
        s++;
    }
}

void order_new_bearing(char id, int bearing) {
    char order[20];
    sprintf(order, "%ct%c\n", id, bearings[bearing].key);
    queue_string(order);
}

void order_new_altitude(char id, int alt) {
    char order[20];
    sprintf(order, "%ca%d\n", id, alt);
    queue_string(order);
}

void land_at_airport(char id, int airport_num) {
    char order[20];
    sprintf(order, "%ctta%d\n%ca0\n", id, airport_num, id);
    queue_string(order);
}

void next_tick() {
    queue_string("\n");
}


bool mark_sense, mark_sent;

void mark_msg() {
    mark_sense = !mark_sense;
    mark_sent = true;
    queue_string(mark_sense ? "zm" : "zu");
}

void de_mark_msg() {
    char str[3] = { erase_char, erase_char, '\0' };
    queue_string(str);
    mark_sent = false;
}
