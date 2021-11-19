#ifndef MEASURE_H_LIB
#define MEASURE_H_LIB
#include <time.h>

static struct timespec s_start;
static struct timespec s_stop;
 
static void set_start() {
    clock_gettime(CLOCK_MONOTONIC_RAW, &s_start);
}

static void set_stop() {
    clock_gettime(CLOCK_MONOTONIC_RAW, &s_stop);
}

static double get_delta_time() {
    return (double)(s_stop.tv_nsec - s_start.tv_nsec)/1e+9
         + (double)(s_stop.tv_sec - s_start.tv_sec);
}

#endif
