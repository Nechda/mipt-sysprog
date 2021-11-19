// simple lib for swap buffers
#ifndef SWP_H_LIB
#define SWP_H_LIB
#include <stddef.h>

#include "shm.h"
#include "sem.h"
#include "config.inc"

struct Swap_buff {
    struct Shared_mem shm;
    struct Semaphore sem;
};

static struct Swap_buff
get_swap_buff(size_t size, char* filename) {
    struct Swap_buff res;

    const size_t MASK = PAGE_SIZE - 1;

    // alloc memory in pages
    size += sizeof(int);
    size = size / PAGE_SIZE + !!(MASK & size);
    size *= PAGE_SIZE;

    res.shm = shared_calloc_local(size, filename);

    struct Semaphore* tmp = gen_semaphores_local(1, filename);
    res.sem = *tmp;
    free(tmp);

    set_sem_val(res.sem, 0);

    // first 4 bytes is for actual bytes in swap_buffer
    res.shm.ptr += sizeof(int);

    return res;
}

static void
set_active_bytes(struct Swap_buff buff, int value) {
    int* ptr = (int*)(buff.shm.ptr - sizeof(int));
    ptr[0] = value;
}

static int
get_active_bytes(struct Swap_buff buff) {
    int* ptr = (int*)(buff.shm.ptr - sizeof(int));
    return *ptr;
}


static void
cleanup_swap(struct Swap_buff buff) {
    buff.shm.ptr -= sizeof(int);
    shared_free(buff.shm);
    semctl(buff.sem.sem_id, 1, IPC_RMID);
}

#endif
