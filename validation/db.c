#include "insider.h"

#define NW    NORTH_WEST
#define  N         NORTH
#define NE    NORTH_EAST
#define  E          EAST
#define SE    SOUTH_EAST
#define  S         SOUTH
#define SW    SOUTH_WEST
#define  W          WEST



struct game_protocol protocol_empty = {
    .name = "empty",
    .geometry = STD_GEOMETRY,
    .geom.std = { 15, 23, 6, 5 },
    .qsteps = 0,
    .steps = NULL,
};



static enum step steps_step12_overflow_bug_example[] = {
    NE, NE, NW,
    SW, S, NE, S,
    NE, NE, W,
    NE, NW, S, S,
    NE, NE, W,
    NE, SE, SW,
    E, NE, NW,
    N, NW, S, S,
    NE, NE, NE,
    SE, SW, N,
    SW, S, E,
    SW, W, N,
    SW, NW, W,
    SW, SE, NE,
    SE, E, E,
    SW, W, NW,
    SW, NW, NW,
    SW, SE, N,
    SE, SW, NW,
    SW, SE, N,
    SE, E, N,
    SE, NE, W,
    NE, SE, E,
    SW, SW, N,
    SW, SE, NE,
    SE, SW, N,
    SW, SE, NE,
    NE, SE, SW, NW, S,
    NE /* Impossible move NE was generated here */
};

struct game_protocol protocol_step12_overflow_bug_example = {
    .name = "step12 overflow bug example",
    .geometry = STD_GEOMETRY,
    .geom.std = { 21, 31, 6, 5 },
    .qsteps = ARRAY_LEN(steps_step12_overflow_bug_example),
    .steps = steps_step12_overflow_bug_example,
};



enum step steps_from_game_002255_loop_in_engine_answer[] = {
    N, NW, NE,
    SE, S, NW, S,
    NW, NW, E,
    NW, NE, S, S,
    NW, NW, NE,
    W, SW, SE,
    W, NW, NE,
    W, SW, SE,
    W, NW, NE,
    NW, W, SE,
    W, W, NW,
    NE, E, SW, S, /* Now engine move contained loops: NE NE NW E W E W E W E W E W E W E W E W E NE */
};

struct game_protocol protocol_002255 = {
    .name = "game 002255 with loop in engine answer",
    .geometry = STD_GEOMETRY,
    .geom.std = { 21, 31, 6, 5 },
    .qsteps = ARRAY_LEN(steps_from_game_002255_loop_in_engine_answer),
    .steps = steps_from_game_002255_loop_in_engine_answer,
};
