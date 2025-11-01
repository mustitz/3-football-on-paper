#ifndef VALIDATION_INSIDER_H
#define VALIDATION_INSIDER_H

#include "paper-football.h"



enum geometry_type {
    STD_GEOMETRY,
    QGEOMETRIES
};

struct std_geom {
    int width;
    int height;
    int goal_width;
    int free_kick_len;
};

union geom_params {
    struct std_geom std;
};

struct game_protocol {
    const char * name;
    enum geometry_type geometry;
    union geom_params geom;
    int qsteps;
    const enum step * steps;
};



struct mcts_ctx
{
    struct geometry * geometry;
    struct ai * ai;
    struct mcts_ai * mcts;

    struct ai ai_storage;
};

extern struct mcts_ctx * restrict const ctx;

void must_init_ctx(
    const struct game_protocol * const protocol);

void free_ctx(void);

struct geometry * must_create_std_geometry(
    const struct std_geom * const params);

struct geometry * must_create_protocol_geometry(
        const struct game_protocol * const protocol);

void must_set_param(
    struct ai * restrict const ai,
    const char * const name,
    const void * const ptr);



void test_fail(const char * const fmt, ...) __attribute__ ((format (printf, 1, 2)));



int test_multialloc(void);
int test_parser(void);
int test_std_geometry(void);
int test_magic_step3(void);
int test_step(void);
int test_step2(void);
int test_history(void);
int test_step12_overflow_error(void);
int test_random_ai(void);
int test_rollout(void);
int test_node_cache(void);
int test_mcts_history(void);
int test_ucb_formula(void);
int test_simulation(void);
int test_random_ai_unstep(void);
int test_mcts_ai_unstep(void);
int test_cycle_detection(void);
int test_ai_no_cycles(void);
int test_preparation(void);

#endif
