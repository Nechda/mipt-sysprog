// simple lib for semaphores
#ifndef SEM_H
#define SEM_H

#include <stdlib.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <stdbool.h>

struct Semaphore {
    volatile int sem_id;
    volatile int idx;
};

static void
set_sem_val(struct Semaphore sem, int val) {
    union semun  
    {
        int val;
        struct semid_ds *buf;
        ushort array [1];
    } sem_attr;
    sem_attr.val = val;
    if (semctl (sem.sem_id, 0, SETVAL, sem_attr) == -1) {
        perror (" semctl SETVAL "); exit (1);
    }
}

static void
set_sem_val_raw(int sem_id, int idx, int val) {
    union semun  
    {
        int val;
        struct semid_ds *buf;
        ushort array [1];
    } sem_attr;
    sem_attr.val = val;
    if (semctl (sem_id, idx, SETVAL, sem_attr) == -1) {
        perror (" semctl SETVAL "); exit (1);
    }
} 


static int
gen_semaphores_raw(int n_sems) {
    static int idx_ = 0;
    key_t key = ftok(__FILE__, idx_++);

    int sem_id = semget(key, n_sems, 0666);
    if(sem_id < 0) {
        sem_id = semget(key, n_sems, 0666 | IPC_CREAT);
    }

    return sem_id;
}

static int
gen_semaphores_raw_key(int n_sems, key_t key) {
    printf("sem_raw_key = %d\n", key);
    int sem_id = semget(key, n_sems, 0666);
    if(sem_id < 0) {
        sem_id = semget(key, n_sems, 0666 | IPC_CREAT);
    }

    return sem_id;
}

static int
gen_semaphores_raw_local(int n_sems, char* filename) {
    static int idx_ = 0;
    key_t key = ftok(filename, idx_++);

    int sem_id = semget(key, n_sems, 0666);
    if(sem_id < 0) {
        sem_id = semget(key, n_sems, 0666 | IPC_CREAT);

    }

    return sem_id;
}

static struct Semaphore*
gen_semaphores(int n_sems) {
    int sem_id = gen_semaphores_raw(n_sems);
    
    struct Semaphore* res = (struct Semaphore*)calloc(n_sems, sizeof(struct Semaphore));
    for(int i = 0; i < n_sems; i++) {
        res[i].idx = i;
        res[i].sem_id = sem_id;
    }

    return res;
}

static struct Semaphore* gen_semaphores_local(int n_sems, char* filename) {
    int sem_id = gen_semaphores_raw_local(n_sems, filename);
    
    struct Semaphore* res = (struct Semaphore*)calloc(n_sems, sizeof(struct Semaphore));
    for(int i = 0; i < n_sems; i++) {
        res[i].idx = i;
        res[i].sem_id = sem_id;
    }

    union semun  
    {
        int val;
        struct semid_ds *buf;
        ushort array [1];
    } sem_attr;
    sem_attr.val = 0;
    
    // clear sems
    for(int i = 0; i < n_sems; i++) {
        if (semctl (sem_id, i, SETVAL, sem_attr) == -1) {
            perror (" semctl SETVAL "); exit (1);
        }
    }

    return res;
}

static void 
A(struct Semaphore sem, int value) {
    struct sembuf s;
    s.sem_num = sem.idx;
    s.sem_op = value;
    semop(sem.sem_id, &s, 1);
}

static void 
D(struct Semaphore sem, int value) {
    struct sembuf s;
    s.sem_num = sem.idx;
    s.sem_op = -value;
    semop(sem.sem_id, &s, 1);
}

static void 
Z(struct Semaphore sem) {
    struct sembuf s;
    s.sem_num = sem.idx;
    s.sem_op = 0;
    semop(sem.sem_id, &s, 1);
}

static void
A_raw(int sem_id, int sem_idx) {
    struct sembuf s;
    s.sem_num = sem_idx;
    s.sem_op = 1;
    semop(sem_id, &s, 1);
}

static void
D_raw(int sem_id, int sem_idx) {
    struct sembuf s;
    s.sem_num = sem_idx;
    s.sem_op = -1;
    semop(sem_id, &s, 1);
}

static void
Z_raw(int sem_id, int sem_idx) {
    struct sembuf s;
    s.sem_num = sem_idx;
    s.sem_op = 0;
    semop(sem_id, &s, 1);
}

#endif