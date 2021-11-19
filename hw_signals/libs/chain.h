#ifndef CHAIN_H_LIB
#define CHAIN_H_LIB
#include "transfer.h"

typedef void (*Action_t)();

struct Chain {
    Action_t action;
    pid_t other;
    int stage;
    bool is_first;
};

static inline void
chain_interation(struct Chain* chain_ptr) {
    bool skip = chain_ptr->stage == 0 && chain_ptr->is_first;
    if(!skip)
        wait_until_ready();
    else
        chain_ptr->is_first = 0;
    chain_ptr->action();
    say_we_ready(chain_ptr->other);
}

#endif