#ifndef TASK_H_LIB
#define TASK_H_LIB

#define RUN_IN_FG(func_) \
    pid = fork(); if(!pid){func_; exit(0);}

#endif