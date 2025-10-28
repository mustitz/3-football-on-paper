#include "paper-football.h"

#include <math.h>
#include <stdio.h>
#include <time.h>

#define ERROR_BUF_SZ   256

#define QPARAMS   4

static const uint32_t    def_qthink =          1024 * 1024;
static const uint32_t     def_cache = CACHE_AUTO_CALCULATE;
static const uint32_t def_max_depth =                  128;
static const  float           def_C =                  1.4;

struct mcts_ai
{
    struct state * state;
    struct state * backup;
    char * error_buf;
    struct ai_param params[QPARAMS+1];
    struct step_stat stats[QSTEPS];

    uint32_t cache;
    uint32_t qthink;
    uint32_t max_depth;
    float    C;

    struct node * nodes;
    uint32_t total_nodes;
    uint32_t used_nodes;
    uint32_t good_node_alloc;
    uint32_t bad_node_alloc;

    struct hist_item * hist;
    struct hist_item * hist_ptr;
    struct hist_item * hist_last;
    uint32_t max_hist_len;

    struct cycle_guard cycle_guard;
    struct cycle_guard backup_cycle_guard;

    struct warns * warns;
};

struct hist_item
{
    uint32_t inode;
    int active;
};

struct node
{
    int32_t score;
    int32_t qgames;
    int32_t children[QSTEPS];
};

static void init_magic_steps(void);
static enum step ai_go(
    struct mcts_ai * restrict const me,
    struct ai_explanation * restrict const explanation);

#define OFFSET(name) offsetof(struct mcts_ai, name)
static struct ai_param def_params[QPARAMS+1] = {
    {    "qthink",    &def_qthink, U32, OFFSET(qthink) },
    {     "cache",     &def_cache, U32, OFFSET(cache) },
    { "max_depth", &def_max_depth, U32, OFFSET(max_depth) },
    {         "C",         &def_C, F32, OFFSET(C) },
    { NULL, NULL, NO_TYPE, 0 }
};

static void * move_ptr(void * ptr, size_t offset)
{
    char * restrict const base = ptr;
    return base + offset;
}

static const uint32_t MIN_CACHE_SZ = (16 * sizeof(struct node));

static void reset_cache(struct mcts_ai * restrict const me)
{
    me->used_nodes = 0;
    me->good_node_alloc = 0;
    me->bad_node_alloc = 0;
}

static void free_cache(struct mcts_ai * restrict const me)
{
    if (me->nodes) {
        free(me->nodes);
        me->nodes = NULL;
    }

    me->total_nodes = 0;
    reset_cache(me);
}

static int init_cache(struct mcts_ai * restrict const me, unsigned int cache_sz)
{
    free_cache(me);

    if (cache_sz == 0) {
        return 0;
    }

    me->nodes = malloc(cache_sz);
    if (me->nodes == NULL) {
        snprintf(me->error_buf, ERROR_BUF_SZ, "Bad alloc %u bytes (nodes).", me->cache);
        return ENOMEM;
    }

    me->total_nodes = cache_sz / sizeof(struct node);
    reset_cache(me);
    return 0;
}

static void calc_cache(
    struct mcts_ai * restrict const me,
    const uint32_t qthink)
{
    unsigned int cache_sz = 4096 + qthink;
    if (cache_sz < MIN_CACHE_SZ) {
        cache_sz = MIN_CACHE_SZ;
    }

    init_cache(me, cache_sz);
}

static int set_cache(
    struct mcts_ai * restrict const me,
    const uint32_t * value)
{
    unsigned int cache_sz = *value;
    if (*value == CACHE_AUTO_CALCULATE) {
        calc_cache(me, me->qthink);
        return 0;
    }

    if (cache_sz < MIN_CACHE_SZ) {
        snprintf(me->error_buf, ERROR_BUF_SZ, "Too small value for cache, minimum is %u.", MIN_CACHE_SZ);
        return EINVAL;
    }

    init_cache(me, cache_sz);
    return 0;
}

static void set_qthink(
    struct mcts_ai * restrict const me,
    const uint32_t * value)
{
    if (me->cache == CACHE_AUTO_CALCULATE) {
        calc_cache(me, *value);
    }
}

static int set_param(
    struct mcts_ai * restrict const me,
    const struct ai_param * const param,
    const void * const value)
{
    const size_t sz = param_sizes[param->type];
    if (sz == 0) {
        return EINVAL;
    }

    int status = 0;
    switch (param->offset) {
        case OFFSET(qthink):
            set_qthink(me, value);
            break;
        case OFFSET(cache):
            status = set_cache(me, value);
            break;
    }

    if (status == 0) {
        void * restrict const ptr = move_ptr(me, param->offset);
        memcpy(ptr, value, sz);
    }

    return status;
}

static void init_param(
    struct mcts_ai * restrict const me,
    const int index)
{
    const struct ai_param * const def_param = def_params + index;
    struct ai_param * restrict const param = me->params + index;
    param->value = move_ptr(me, param->offset);
    set_param(me, param, def_param->value);
}

static void free_ai(struct mcts_ai * restrict const me)
{
    free_cache(me);
    if (me->hist) {
        free(me->hist);
    }
    free_state(me->state);
    free_state(me->backup);
    free(me);
}

static struct mcts_ai * create_mcts_ai(const struct geometry * const geometry)
{
    init_magic_steps();

    const uint32_t qpoints = geometry->qpoints;
    const uint32_t free_kick_len = geometry->free_kick_len;
    const uint32_t free_kick_reduce = (free_kick_len - 1) * (free_kick_len - 1);
    const size_t cycle_guard_capacity = 4 + qpoints / free_kick_reduce;
    const size_t sizes[8] = {
        sizeof(struct mcts_ai),
        sizeof(struct state),
        qpoints,
        sizeof(struct state),
        qpoints,
        cycle_guard_capacity * sizeof(struct kick),
        cycle_guard_capacity * sizeof(struct kick),
        ERROR_BUF_SZ
    };

    void * ptrs[8];
    void * data = multialloc(8, sizes, ptrs, 64);

    if (data == NULL) {
        return NULL;
    }

    struct mcts_ai * restrict const me = data;
    struct state * restrict const state = ptrs[1];
    uint8_t * restrict const lines = ptrs[2];
    struct state * restrict const backup = ptrs[3];
    uint8_t * restrict const backup_lines = ptrs[4];
    struct kick * restrict cycle_guard_kicks = ptrs[5];
    struct kick * restrict backup_cycle_guard_kicks = ptrs[6];
    char * const error_buf = ptrs[7];

    me->state = state;
    me->backup = backup;
    me->error_buf = error_buf;

    me->nodes = NULL;
    reset_cache(me);

    me->hist = NULL;
    me->hist_last = NULL;
    me->hist_ptr = NULL;
    me->max_hist_len = 0;

    me->cycle_guard.capacity = cycle_guard_capacity;
    me->cycle_guard.kicks = cycle_guard_kicks;
    cycle_guard_reset(&me->cycle_guard);

    me->backup_cycle_guard.capacity = cycle_guard_capacity;
    me->backup_cycle_guard.kicks = backup_cycle_guard_kicks;
    cycle_guard_reset(&me->backup_cycle_guard);

    memcpy(me->params, def_params, sizeof(me->params));
    for (int i=0; i<QPARAMS; ++i) {
        init_param(me, i);
    }

    init_state(state, geometry, lines);
    init_state(backup, geometry, backup_lines);
    return me;
}

static void free_mcts_ai(struct ai * restrict const ai)
{
    free_history(&ai->history);
    free_ai(ai->data);
}

static int mcts_ai_reset(
    struct ai * restrict const ai,
    const struct geometry * const geometry)
{
    ai->error = NULL;

    struct mcts_ai * restrict const me = create_mcts_ai(geometry);
    if (me == NULL) {
        snprintf(me->error_buf, ERROR_BUF_SZ, "Bad alloc for create_mcts_ai.");
        ai->error = me->error_buf;
        return errno;
    }

    const struct ai_param * ptr = ai->get_params(ai);
    for (; ptr->name != NULL; ++ptr) {
        const int status = set_param(me, ptr, ptr->value);
        if (status != 0) {
            snprintf(me->error_buf, ERROR_BUF_SZ, "Cannot set parameter %s for new instance, status is %d.", ptr->name, status);
            ai->error = me->error_buf;
            free_ai(me);
            return status;
        }
    }

    free_ai(ai->data);
    ai->data = me;
    return 0;
}

static void save_state(
    struct mcts_ai * restrict const me)
{
    state_copy(me->backup, me->state);

    const size_t qkicks = me->cycle_guard.qkicks;
    me->backup_cycle_guard.qkicks = qkicks;
    if (qkicks > 0) {
        const size_t sz = qkicks * sizeof(struct kick);
        memcpy(me->backup_cycle_guard.kicks, me->cycle_guard.kicks, sz);
    }
}

static void restore_backup(struct mcts_ai * restrict const me)
{
    struct state * old_state = me->state;
    me->state = me->backup;
    me->backup = old_state;

    struct kick * old_kicks = me->cycle_guard.kicks;
    me->cycle_guard.kicks = me->backup_cycle_guard.kicks;
    me->backup_cycle_guard.kicks = old_kicks;
    me->cycle_guard.qkicks = me->backup_cycle_guard.qkicks;
}

static steps_t forbid_cycles(
    struct mcts_ai * restrict const me,
    struct cycle_guard * restrict const cycle_guard,
    struct state * restrict state,
    steps_t steps)
{
    steps_t cycles = 0;
    steps_t tmp = steps;
    while (tmp != 0) {
        enum step step = extract_step(&tmp);
        int from = state->ball;
        int to = state->geometry->free_kicks[QSTEPS * from + step];

        enum cycle_result status = cycle_guard_push(cycle_guard, from, to);
        switch (status) {
            case NO_CYCLE:
                cycle_guard_pop(cycle_guard);
                break;
            case CYCLE_FOUND:
                cycles |= 1 << step;
                break;
        }
    }

    if (steps != cycles) {
        return steps ^ cycles;
    }

    WARN(me->warns, STEPS_ARE_CYCLES, "steps", steps, "cycles", cycles);

    static const steps_t player1_priority[QSTEPS] = {
        1 << NORTH,
        1 << NORTH_WEST,
        1 << NORTH_EAST,
        1 << EAST,
        1 << WEST,
        1 << SOUTH_WEST,
        1 << SOUTH_EAST,
        1 << SOUTH,
    };

    static const steps_t player2_priority[QSTEPS] = {
        1 << SOUTH,
        1 << SOUTH_WEST,
        1 << SOUTH_EAST,
        1 << EAST,
        1 << WEST,
        1 << NORTH_WEST,
        1 << NORTH_EAST,
        1 << NORTH,
    };

    const steps_t * priority;
    switch (state->active) {
        case 1:
            priority = player1_priority;
            break;
        case 2:
            priority = player2_priority;
            break;
        default:
            WARN(me->warns, ACTIVE_OOR, "active", state->active, NULL, 0);
            return steps;
    }

    for (int i=0; i<QSTEPS; ++i) {
        steps_t mask = priority[i];
        if (steps & mask) {
            return mask;
        }
    }

    WARN(me->warns, INCONSISTERN_STEPS_PRIORITY, "steps", steps, "active", state->active);
    return steps;
}

static int state_step_proxy(
    struct mcts_ai * restrict const me,
    const enum step step)
{
    struct state * restrict const state = me->state;
    const int old_ball = state->ball;
    const int is_free_kick = is_free_kick_situation(state);

    const int result = state_step(state, step);
    if (result < 0) {
        return result;
    }

    if (is_free_kick) {
        cycle_guard_push(&me->cycle_guard, old_ball, result);
    } else {
        cycle_guard_reset(&me->cycle_guard);
    }

    return result;
}

static int mcts_ai_do_step(
    struct ai * restrict const ai,
    const enum step step)
{
    ai->error = NULL;
    struct mcts_ai * restrict const me = ai->data;

    const int next = state_step_proxy(me, step);

    if (next == NO_WAY) {
        snprintf(me->error_buf, ERROR_BUF_SZ, "Direction occupied.");
        ai->error = me->error_buf;
        return EINVAL;
    }

    struct history * restrict const history = &ai->history;
    const int status = history_push(history, me->state);
    if (status != 0) {
        snprintf(me->error_buf, ERROR_BUF_SZ, "Bad history push, return code is %d.", status);
        return status;
    }

    return 0;
}

static int mcts_ai_do_steps(
    struct ai * restrict const ai,
    const unsigned int qsteps,
    const enum step steps[])
{
    ai->error = NULL;
    struct mcts_ai * restrict const me = ai->data;

    struct history * restrict const history = &ai->history;
    const unsigned int old_qstep_changes = history->qstep_changes;

    save_state(me);

    const enum step * ptr = steps;
    const enum step * const end = ptr + qsteps;
    for (; ptr != end; ++ptr) {
        const int next = state_step_proxy(me, *ptr);
        if (next == NO_WAY) {
            const int index = ptr - steps;
            snprintf(me->error_buf, ERROR_BUF_SZ, "Error on step %d: direction  occupied.", index);
            ai->error = me->error_buf;
            restore_backup(me);
            history->qstep_changes = old_qstep_changes;
            return EINVAL;
        }

        const int status = history_push(history, me->state);
        if (status != 0) {
            const int index = ptr - steps;
            snprintf(me->error_buf, ERROR_BUF_SZ, "Bad history push on step %d, return code is %d.", index, status);
            ai->error = me->error_buf;
            restore_backup(me);
            history->qstep_changes = old_qstep_changes;
            return status;
        }
    }

    return 0;
}

static int mcts_ai_undo_steps(
    struct ai * restrict const ai,
    unsigned int qsteps)
{
    if (qsteps == 0) {
        return 0;
    }

    ai->error = NULL;
    struct mcts_ai * restrict const me = ai->data;

    struct history * restrict const history = &ai->history;
    if (history->qstep_changes == 0) {
        return EINVAL;
    }

    const struct step_change * last_change = history->step_changes + history->qstep_changes;
    const int what = last_change[-1].what;
    if (what != CHANGE_PASS && what != CHANGE_FREE_KICK) {
        return EINVAL;
    }

    --qsteps;

    const struct step_change * ptr = last_change - 1;
    const struct step_change * const end = history->step_changes;
    for (;;) {
        if (ptr == end) break;
        const int what = ptr[-1].what;
        if (what == CHANGE_PASS || what == CHANGE_FREE_KICK) {
            if (qsteps == 0) {
                break;
            }
            --qsteps;
        }
        --ptr;
    }

    const unsigned int qstep_changes = last_change - ptr;
    state_rollback(me->state, ptr, qstep_changes);
    history->qstep_changes -= qstep_changes;
    cycle_guard_reset(&me->cycle_guard);
    return 0;
}

static int mcts_ai_undo_step(struct ai * restrict const ai)
{
    return mcts_ai_undo_steps(ai, 1);
}

static enum step mcts_ai_go(
    struct ai * restrict const ai,
    struct ai_explanation * restrict const explanation)
{
    ai->error = NULL;
    struct mcts_ai * restrict const me = ai->data;
    const enum step step = ai_go(me, explanation);
    if (step == INVALID_STEP) {
        ai->error = me->error_buf;
    }
    return step;
}

static const struct ai_param * mcts_ai_get_params(const struct ai * const ai)
{
    struct mcts_ai * restrict const me = ai->data;
    return me->params;
}

static const struct ai_param * find_param(
    struct mcts_ai * restrict const me,
    const char * const name)
{
    for (int i=0; i<QPARAMS; ++i) {
        const struct ai_param * const param = me->params + i;
        if (strcasecmp(name, param->name) == 0) {
            return param;
        }
    }

    return NULL;
}

static int mcts_ai_set_param(
    struct ai * restrict const ai,
    const char * const name,
    const void * const value)
{
    ai->error = NULL;

    struct mcts_ai * restrict const me = ai->data;
    const struct ai_param * const param = find_param(me, name);
    if (param == NULL) {
        return EINVAL;
    }

    const int status = set_param(me, param, value);
    if (status != 0) {
        ai->error = me->error_buf;
    }
    return status;
}

static const struct state * mcts_ai_get_state(const struct ai * const ai)
{
    struct mcts_ai * restrict const me = ai->data;
    return me->state;
}

int init_dev_0003_ai(
    struct ai * restrict const ai,
    const struct geometry * const geometry)
{
    ai->error = NULL;

    if (geometry == NULL) {
        ai->error = "Argument “geometry” cannot be NULL.";
        return EINVAL;
    }

    ai->data = create_mcts_ai(geometry);
    if (ai->data == NULL) {
        ai->error = "Bad alloc for create_mcts_ai.";
        return errno;
    }

    struct mcts_ai * restrict const me = ai->data;
    me->warns = &ai->warns;

    init_history(&ai->history);
    warns_init(&ai->warns);

    ai->reset = mcts_ai_reset;
    ai->do_step = mcts_ai_do_step;
    ai->do_steps = mcts_ai_do_steps;
    ai->undo_step = mcts_ai_undo_step;
    ai->undo_steps = mcts_ai_undo_steps;
    ai->go = mcts_ai_go;
    ai->get_params = mcts_ai_get_params;
    ai->set_param = mcts_ai_set_param;
    ai->get_state = mcts_ai_get_state;
    ai->get_warn = ai_get_warn;
    ai->free = free_mcts_ai;

    return 0;
}



/* AI step selection */

static enum step magic_steps[256][8];

static void init_magic_steps(void)
{
    if (magic_steps[1][1] == 1) {
        return;
    }

    for (uint32_t mask=0; mask<256; ++mask) {
        steps_t steps = mask;
        for (int n=0; n<8; ++n) {
            if (steps == 0) {
                magic_steps[mask][n] = INVALID_STEP;
            } else {
                enum step step = extract_step(&steps);
                magic_steps[mask][n] = step;
            }
        }
    }
}

static struct node * alloc_node(struct mcts_ai * restrict const me)
{
    if (me->used_nodes >= me->total_nodes) {
        ++me->bad_node_alloc;
        return NULL;
    }

    struct node * restrict const result = me->nodes + me->used_nodes;
    ++me->good_node_alloc;
    ++me->used_nodes;
    memset(result, 0, sizeof(struct node));
    return result;
}

static inline enum step random_step(steps_t steps)
{
    enum step alternatives[QSTEPS];
    int qalternatives = 0;
    for (enum step step=0; step<QSTEPS; ++step) {
        const steps_t mask = 1 << step;
        if (mask & steps) {
            alternatives[qalternatives++] = step;
        }
    }
    const int choice = rand() % qalternatives;
    return alternatives[choice];
}

static int rollout(
    struct state * restrict const state,
    uint32_t max_steps,
    uint32_t * qthink)
{
    for (;;) {
        const int status = state_status(state);

        if (status == WIN_1) {
            return +1;
        }

        if (status == WIN_2) {
            return -1;
        }

        if (max_steps-- == 0) {
            return 0;
        }

        steps_t answers = state_get_steps(state);
        if (answers == 0) {
            return state->active != 1 ? +1 : -1;
        }

        const int multiple_ways = answers & (answers - 1);
        const enum step step = multiple_ways ? random_step(answers) : first_step(answers);

        state_step(state, step);
        ++*qthink;
    }
}

static void update_history(
    struct mcts_ai * restrict const me,
    const int32_t score)
{
    const struct hist_item * ptr = me->hist;
    const struct hist_item * const end = me->hist_ptr;
    for (; ptr != end; ++ptr) {
        struct node * restrict const node = me->nodes + ptr->inode;
        ++node->qgames;
        node->score += ptr->active == 1 ? score : -score;
    }

    const uint32_t hist_len = me->hist_ptr - me->hist;
    if (hist_len > me->max_hist_len) {
        me->max_hist_len = hist_len;
    }
}

static void add_history(
    struct mcts_ai * restrict const me,
    struct node * restrict const node,
    const int active)
{
    if (me->hist_ptr != me->hist_last) {
        me->hist_ptr->inode = node - me->nodes;
        me->hist_ptr->active = active;
        ++me->hist_ptr;
        return;
    }

    const size_t hist_capacity = me->hist_last - me->hist;
    const size_t new_hist_capacity = 128 + 2 * hist_capacity;
    const size_t new_history_sz = new_hist_capacity * sizeof(struct hist_item);
    struct hist_item * restrict const new_hist = realloc(me->hist, new_history_sz);
    if (new_hist == NULL) {
        return;
    }

    me->hist_ptr += new_hist - me->hist;
    me->hist = new_hist;
    me->hist_last = new_hist + new_hist_capacity;
    me->hist_ptr->inode = node - me->nodes;
    me->hist_ptr->active = active;
    ++me->hist_ptr;
}

static enum step select_step(
    const struct mcts_ai * const me,
    const struct node * const node,
    steps_t steps)
{
    int qbest = 0;
    enum step best_steps[QSTEPS];
    float best_weight = -1.0e+10f;

    const int multiple_ways = steps & (steps - 1);
    if (!multiple_ways) {
        const enum step choice = first_step(steps);
        return choice;
    }

    const float total = node->qgames;
    const float log_total = log(total);
    while (steps != 0) {
        const enum step step = extract_step(&steps);
        const struct node * const child = me->nodes + node->children[step];
        const float score = child->score;
        const float qgames = child->qgames;
        const float ev = score / qgames;
        const float investigation = sqrt(log_total/qgames);
        const float weight = ev + me->C * investigation;

        if (weight >= best_weight) {
            if (weight != best_weight) {
                qbest = 0;
                best_weight = weight;
            }
            best_steps[qbest++] = step;
        }
    }

    const int index = qbest == 1 ? 0 : rand() % qbest;
    const enum step choice = best_steps[index];
    return choice;
}

static uint32_t simulate(
    struct mcts_ai * restrict const me,
    struct node * restrict node)
{
    struct state * restrict const state = me->backup;
    struct cycle_guard * restrict const cycle_guard = &me->backup_cycle_guard;
    save_state(me);

    if (state->ball == GOAL_1) {
        return 1;
    }

    if (state->ball == GOAL_2) {
        return 1;
    }

    uint32_t qthink = 1;
    me->hist_ptr = me->hist;

    for (;;) {

        steps_t answers = state_get_steps(state);
        if (answers == 0) {
            update_history(me, state->active != 1 ? +1 : -1);
            return qthink;
        }

        const int is_free_kick = is_free_kick_situation(state);
        const int multiple_ways = answers & (answers - 1);
        if (multiple_ways) {
            if (is_free_kick) {
                answers = forbid_cycles(me, cycle_guard, state, answers);
            }
        }

        const enum step step = select_step(me, node, answers);
        ++qthink;

        uint32_t ichild = node->children[step];
        if (ichild == 0) {
            struct node * restrict const child = alloc_node(me);
            if (child == NULL) {
                return 0;
            }
            node->children[step] = child - me->nodes;
            node = child;
        } else {
            node = me->nodes + ichild;
        }

        add_history(me, node, state->active);

        int old_ball = state->ball;
        int old_active = state->active;
        state_step(state, step);
        const int status = state_status(state);

        if (status == WIN_1) {
            update_history(me, +1);
            return qthink;
        }

        if (status == WIN_2) {
            update_history(me, -1);
            return qthink;
        }

        if (ichild == 0) {
            break;
        }

        if (is_free_kick && state->active == old_active) {
            cycle_guard_push(cycle_guard, old_ball, state->ball);
        } else {
            cycle_guard_reset(cycle_guard);
        }
    }

    const int32_t score = rollout(state, me->max_depth, &qthink);
    update_history(me, score);
    return qthink;
}

static int compare_stats(
    const void * const ptr_a,
    const void * const ptr_b)
{
    const struct step_stat * a = ptr_a;
    const struct step_stat * b = ptr_b;
    if (a->qgames > b->qgames) return -1;
    if (a->qgames < b->qgames) return +1;
    return 0;
}

static enum step ai_go(
    struct mcts_ai * restrict const me,
    struct ai_explanation * restrict const explanation)
{
    warns_reset(me->warns);

    if (explanation) {
        explanation->qstats = 0;
        explanation->stats = NULL;
        explanation->time = 0.0;
        explanation->score = -1.0;
        explanation->cache.used = 0;
        explanation->cache.total = 0;
        explanation->cache.good_alloc = 0;
        explanation->cache.bad_alloc = 0;
    }

    struct state * restrict state = me->state;

    steps_t steps = state_get_steps(state);
    if (steps == 0) {
        snprintf(me->error_buf, ERROR_BUF_SZ, "no possible steps.");
        return INVALID_STEP;
    }

    int multiple_ways = steps & (steps - 1);
    if (multiple_ways) {
        const int is_free_kick = is_free_kick_situation(state);
        if (is_free_kick) {
            steps = forbid_cycles(me, &me->cycle_guard, state, steps);
            multiple_ways = steps & (steps - 1);
        }
    }

    if (!multiple_ways) {
        const enum step choice = first_step(steps);
        return choice;
    }

    double start = clock();

    reset_cache(me);

    struct node * restrict const zero = alloc_node(me);
    if (zero == NULL) {
        snprintf(me->error_buf, ERROR_BUF_SZ, "alloc zero node failed.");
        return INVALID_STEP;
    }
    zero->score = 2;
    zero->qgames = 1;

    struct node * restrict const root = alloc_node(me);
    if (root == NULL) {
        snprintf(me->error_buf, ERROR_BUF_SZ, "alloc root node failed.");
        return INVALID_STEP;
    }

    root->qgames = 1;
    uint32_t qthink = 0;
    for (;;) {
        const uint32_t delta_think = simulate(me, root);
        if (delta_think == 0) {
            break;
        }

        qthink += delta_think;
        ++root->qgames;

        if (qthink >= me->qthink) {
            break;
        }
    }

    int qbest = 0;
    int32_t best_qgames = -2147483648;
    enum step best_steps[QSTEPS];

    for (enum step step=0; step<QSTEPS; ++step) {
        const uint32_t ichild = root->children[step];
        if (ichild == 0) {
            continue;
        }

        const struct node * const child = me->nodes + ichild;
        int32_t qgames = child->qgames;

        if (qgames >= best_qgames) {
            if (qgames > best_qgames) {
                qbest = 0;
                best_qgames = qgames;
            }
            best_steps[qbest++] = step;
        }
    }

    const int index = qbest == 1 ? 0 : rand() % qbest;
    enum step result = best_steps[index];

    if (explanation) {
        double finish = clock();
        explanation->time = (finish - start) / CLOCKS_PER_SEC;

        size_t qstats = 1;
        for (enum step step=0; step<QSTEPS; ++step) {
            const uint32_t ichild = root->children[step];
            if (ichild == 0) {
                continue;
            }

            const struct node * const child = me->nodes + ichild;
            const int32_t qgames = child->qgames;
            const int32_t score = child->score;
            double norm_score = -1.0;
            if (qgames > 0) {
                norm_score = 0.5 * (score + qgames) / (double)qgames;
            }

            const size_t i = step == result ? 0 : qstats;
            me->stats[i].step = step;
            me->stats[i].qgames = child->qgames;
            me->stats[i].score = norm_score;
            qstats += !!i;
        }

        explanation->qstats = qstats;
        explanation->stats = me->stats;

        explanation->score = me->stats[0].score;
        if (state->active == 2) {
            explanation->score = 1.0 - explanation->score;
        }

        if (qstats > 2) {
            qsort(me->stats + 1, qstats - 1, sizeof(struct step_stat), compare_stats);
        }

        // Fill cache statistics in explanation
        explanation->cache.used = me->used_nodes;
        explanation->cache.total = me->total_nodes;
        explanation->cache.good_alloc = me->good_node_alloc;
        explanation->cache.bad_alloc = me->bad_node_alloc;
    }

    return result;
}
