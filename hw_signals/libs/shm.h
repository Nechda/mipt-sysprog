// simple lib for shared memory
#ifndef SHM_H_LIB
#define SHM_H_LIB
#include <stddef.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include "sem.h"
#include "config.inc"

struct Shared_mem {
    char* ptr;
    size_t size;
    int shm_id;
    int sync;
};

static void lock_shm(struct Shared_mem shm) {
    printf("pid = %d, sync = %d\n", getpid(), shm.sync);
    struct sembuf s[2];
    s[0].sem_num = 0;
    s[0].sem_op = 0;
    s[1].sem_num = 0;
    s[1].sem_op = 1;
    semop(shm.sync, &s[0], 2);
}

static void unlock_shm(struct Shared_mem shm) {
    D_raw(shm.sync, 0);
}

struct Shared_mem shared_calloc(size_t size) {
    static int index__ = 0;
    key_t key = ftok(__FILE__, index__++);

    const size_t MASK = PAGE_SIZE - 1;

    // alloc memory in pages
    size += sizeof(int);
    size = size / PAGE_SIZE + !!(MASK & size);
    size *= PAGE_SIZE;

    int shm_id = 0;
    shm_id = shmget(key, size, 0666);
    if(shm_id < 0) {
        shm_id = shmget(key, size, 0666 | IPC_CREAT);
    }

    struct Shared_mem res;
    res.ptr = (char*)shmat(shm_id, NULL, 0);
    res.size = size;
    res.shm_id = shm_id;
    //res.sync = gen_semaphores_raw_local(1, __FILE__);

    return res;
}

struct Shared_mem shared_calloc_local(size_t size, char* filename) {
    static int index__ = 0;
    key_t key = ftok(filename, index__++);

    const size_t MASK = PAGE_SIZE - 1;

    // alloc memory in pages
    size += sizeof(int);
    size = size / PAGE_SIZE + !!(MASK & size);
    size *= PAGE_SIZE;

    int shm_id = 0;
    shm_id = shmget(key, size, 0666 | IPC_CREAT);

    struct Shared_mem res;
    res.ptr = (char*)shmat(shm_id, NULL, 0);
    res.size = size;
    res.shm_id = shm_id;
    //res.sync = gen_semaphores_raw_local(1, filename);

    return res;
}

struct Shared_mem shared_calloc_key(size_t size, key_t key) {
    const size_t MASK = PAGE_SIZE - 1;

    // alloc memory in pages
    size += sizeof(int);
    size = size / PAGE_SIZE + !!(MASK & size);
    size *= PAGE_SIZE;

    int shm_id = 0;
    shm_id = shmget(key, size, 0666 | IPC_CREAT);

    struct Shared_mem res;
    res.ptr = (char*)shmat(shm_id, NULL, 0);
    res.size = size;
    res.shm_id = shm_id;
    res.sync = gen_semaphores_raw_key(1, key);

    return res;
}

void shared_free(struct Shared_mem mem) {
    if(!mem.ptr) return;

    semctl(mem.sync, 1, IPC_RMID);
    shmdt(mem.ptr);
    shmctl(mem.shm_id, IPC_RMID, NULL);
}

#endif
