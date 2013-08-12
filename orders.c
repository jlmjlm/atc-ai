#include <stdio.h>
#include "atc-ai.h"

const struct bearing bearings[8] = {
    { 0, -1, 0, 'w', '^', "N", "north" },
    { 45, -1, 1, 'e', '\0', "NE", "northeast" },
    { 90, 0, 1, 'd', '>', "E", "east" },
    { 135, 1, 1, 'c', '\0', "SE", "southeast" },
    { 180, 1, 0, 'x', 'v', "S", "south" },
    { 225, 1, -1, 'z', '\0', "SW", "southwest" },
    { 270, 0, -1, 'a', '<', "W", "west" },
    { 315, -1, -1, 'q', '\0', "NW", "northwest" },
};