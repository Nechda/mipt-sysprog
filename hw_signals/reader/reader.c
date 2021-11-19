#include <fcntl.h>
#include <memory.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/signalfd.h>

#include "libs/dbg.h"
#include "libs/sem.h"
#include "libs/shm.h"
#include "libs/swp.h"
#include "libs/task.h"
#include "libs/chain.h"
#include "libs/transfer.h"

// for constants
#include "config.inc"

// for speed measuring
#include "libs/measure.h"

pid_t child_pids[N_CHILDS] = {};
struct Semaphore master_chid_sem;
struct Shared_mem buf_for_cfgs;
struct Shared_mem buf_for_file;
struct Shared_mem shm_for_pids;
struct Chain reader_chain;

struct Sender_child_cfg {
    int cur_buf_idx;
    int n_bytes_to_send;
    pid_t pid_to_send;
};

/*
    ===============================================================================================
*/

// Initial cleaning of structures
void default_setting_up() {
    #define cls(var__) memset(&var__, 0, sizeof(var__))
    cls(master_chid_sem);
    cls(buf_for_cfgs);
    cls(shm_for_pids);
    #undef cls
}

/*
    ===============================================================================================
*/

// 1. create pair of semaphores for sync sender & reader childs during transfer
void init_inter_process_sems() {
    reader_chain.stage = 1;
    reader_chain.is_first = 1;
}

// 2. create local semaphores for sync sender_master & childs
void init_local_group_sems() {
    struct Semaphore* tmp = gen_semaphores_local(1, __FILE__);
    master_chid_sem = *tmp;
    free(tmp);

    logger("[main] [init] master_chid_sem.sem_id = %d", master_chid_sem.sem_id);
}

// 3. create shared buf_for_cgfs
void init_buf_for_cfg() {
    buf_for_cfgs = shared_calloc_local(sizeof(struct Sender_child_cfg) * N_CHILDS, __FILE__);
    struct Sender_child_cfg* cfg = (struct Sender_child_cfg*) buf_for_cfgs.ptr;
    FOREACH_CHILD(i)
        cfg[i].cur_buf_idx = 0;
}

// 4. init swap_buffers
void init_buf_for_readed_bytes() {
    buf_for_file = shared_calloc_local(SWAP_BUF_SIZE, __FILE__);
}

void init() {
    printf("pid = %d\n", getpid());
    default_setting_up();
    init_signal_ranges();
    init_inter_process_sems();
    init_local_group_sems();
    init_buf_for_cfg();
    init_buf_for_readed_bytes();
}

void full_cleanup() {
    // delete shared-mem objects
    shared_free(buf_for_cfgs);
    shared_free(buf_for_file);
    shared_free(shm_for_pids);
    // delete local semaphores
    semctl(master_chid_sem.sem_id, 2, IPC_RMID);
}

/*
    ===============================================================================================
*/

void work_for_child(int internal_index) {
    struct Sender_child_cfg* cfgs = (struct Sender_child_cfg*)buf_for_cfgs.ptr;
    cfgs = &cfgs[internal_index];
    char* buff__ = buf_for_file.ptr + BUFFER_SIZE * internal_index;

    struct signalfd_siginfo fdsi;
    int sfd = init_signals_for_reader();
    int r_ = 0;

    // increment number of ready readers
    A(master_chid_sem, 1);
    while(1) {
        r_ = read(sfd, &fdsi, sizeof(struct signalfd_siginfo));
        if(r_ != sizeof(struct signalfd_siginfo))
            handle_error("Problems with reading signal from sfd");

        if(fdsi.ssi_signo == SIGUSR1) {
            logger("[child] getted SIGUSR1");
            logger("[child] is ready to read buf");
            read_buff_via_signal(
                sfd,
                buff__,
                fdsi.ssi_pid
            );
            // this field countain amount of bytes, that really sended
            cfgs->n_bytes_to_send = fdsi.ssi_int; 

            A(master_chid_sem, 1);
        }
        if(fdsi.ssi_signo == SIGUSR2) {
            logger("[child] getted SIGUSR2"); 
            cfgs->n_bytes_to_send = 0;
            A(master_chid_sem, 1);
        }
        if(fdsi.ssi_signo == SIGQUIT) {
            logger("[child] getted SIGQUIT"); 
            cfgs->n_bytes_to_send = 0;
            A(master_chid_sem, 1);
            break;
        }
    }
}

/*
    ===============================================================================================
*/

int need_write____  = 0;
int out_file_descriptor = 0;

void reader_chain_action() {
    static bool is_first = 1;

    volatile struct Sender_child_cfg* cfgs = (struct Sender_child_cfg*)buf_for_cfgs.ptr;

    logger("[main] wait untill all childs ready");
    D(master_chid_sem, N_CHILDS);
    if(is_first) {
        set_start();
        is_first = 0;
    }
    // after this line all childs readed it's piece of data
    logger("[main] All childs are cussesfully read data");

    // count total amount of recieved bytes
    int need_write = 0;
    FOREACH_CHILD(i)
        need_write += cfgs[i].n_bytes_to_send;

    need_write____ = need_write;
    if(!need_write) return;

    // and write it from the shared buffer
    write(out_file_descriptor, buf_for_file.ptr, need_write);
    logger("[main] Data writed in file");
}

size_t main_loop(int fd) {
    size_t bytes_in_file = 0;

    out_file_descriptor = fd;
    reader_chain.action = reader_chain_action;
    do {
        chain_interation(&reader_chain);
        logger("[main] bytes = %d", need_write____);
        bytes_in_file += need_write____;
    }while(need_write____ > 0);
    logger("[main] exit from loop");

    close(out_file_descriptor);
    set_stop();

    return bytes_in_file;
}



static volatile int sender_pid = 0;

static void send_pids_thread() {
    int sfd = init_signals_for_reader();
    if(sfd < 0) handle_error("send_pids_thread -- sfd");
    FOREACH_CHILD(i) {
        logger("[main] Send");
        union sigval val;
        val.sival_int = child_pids[i];
        sigqueue(sender_pid, SIGUSR1, val);
        wait_until_readyp(sender_pid);
    }
    logger("[main] Done");
}

static void helper_handler(int sig_num, siginfo_t * info, void * context) {
    sender_pid = info->si_pid;
}

void send_pids() {
    #if 1
    logger("[main] Setting up SIGUSR2 handler");
    // create handler
    struct sigaction act_1;
    // ?? why warning
    act_1.sa_sigaction = helper_handler;
    act_1.sa_flags = SA_SIGINFO;
    sigaction(SIGUSR2, &act_1, NULL);

    logger("[main] Wait for SIGUSR2");
    // wait first signal from sender
    static sigset_t set;
    int sig;
    int *sigptr = &sig;
    sigemptyset(&set);
    sigaddset(&set, SIGUSR2);
    sigsuspend(NULL);

    while(!sender_pid);
    logger("[main] Start sending");

    send_pids_thread();
    reader_chain.other = sender_pid;

    #else
    // here was better solution ...
    char filename[128];
    sprintf(filename, "/proc/%d/stack", getpid());
    shm_for_pids = shared_calloc_key(sizeof(pid_t) * N_CHILDS + sizeof(int), ftok(filename,0));
    lock_shm(shm_for_pids);
    int* ibuf_ptr = shm_for_pids.ptr;
    ibuf_ptr[0] = shm_for_pids.sync;
    ibuf_ptr++;
    FOREACH_CHILD(i)
        ibuf_ptr[i] = child_pids[i];
    unlock_shm(shm_for_pids);
    #endif
    return;
}

static void kill_all(int sig) {
    FOREACH_CHILD(i)
        kill(child_pids[i], SIGKILL);
    full_cleanup();
    // also interupt sender
    kill(sender_pid, SIGINT);
    exit(EXIT_FAILURE);
}

void set_sigint_handler() {
    struct sigaction act;
    act.sa_handler = &kill_all;    
    act.sa_flags = SA_RESTART; 
    if(sigaction(SIGINT, &act, NULL) < 0)
        handle_error("Cant set SIGINT handler");
}

void sub_process_creation() {
    // create childs
    pid_t pid = 0;
    FOREACH_CHILD(i) {
        child_pids[i] = RUN_IN_FG(work_for_child(i));
    }
    // wait unil childs is not ready to read signals
    D(master_chid_sem, N_CHILDS);
}

static void measure_speed(size_t bytes_in_file) {
    double total_time = get_delta_time();
    double size_in_mb = (double) bytes_in_file / (1024.0 * 1024.0);
    double speed = size_in_mb / total_time;

    printf("Total time = %.2lf sec\n", total_time);
    printf("Size = %.2lf Mb\n", size_in_mb);
    printf("Speed = %.2lf Mb/sec\n", speed);
}

int main(int argc, char** argv) {

    if(argc != 2) {
        printf("Use as first cmd arg output filename.\n");
        return EXIT_FAILURE;
    }

    int fd = open(argv[1], O_WRONLY | O_TRUNC | O_CREAT | O_CLOEXEC, 0666);
    if(fd < 0)
        handle_error("Cant open output file for writing.");

    init();
    sub_process_creation();
    send_pids();
    set_sigint_handler();

    // start transfer main loop
    size_t bytes_in_file = main_loop(fd);

    // send SIGQUIT for stop childs
    FOREACH_CHILD(i) {
        union sigval val;
        val.sival_int = 0;
        sigqueue(child_pids[i], SIGQUIT, val);
    }

    FOREACH_CHILD(i) {
        int wstatus = 0;
        pid_t pid = wait(&wstatus);
        if(WIFEXITED(wstatus) == 1) {
            int ret_code = WEXITSTATUS(wstatus);
            logger("[main] ret_code[%d] = %d", i, ret_code);
        }
    }

    full_cleanup();

    measure_speed(bytes_in_file);
    return 0;    
}
