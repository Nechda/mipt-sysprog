#include <errno.h>
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

pid_t child_pids[N_CHILDS] = {};
pid_t reader_thread_pid = 0;
pid_t reader_process_pid = 0;
struct Semaphore master_chid_sem;
struct Shared_mem buf_for_cfgs;
struct Shared_mem shm_for_pids;
struct Swap_buff swap_buffer[N_SWAP_BUFFERS] = {};
struct Chain sender_chain;

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
    cls(swap_buffer);
    cls(sender_chain);
    #undef cls
}

/*
    ===============================================================================================
*/

// 1. create pair of semaphores for sync sender & reader childs during transfer
void init_inter_process_sems(int reader_pid) {
    sender_chain.stage = 0;
    sender_chain.is_first = 1;
    sender_chain.other = reader_pid;
}

// 2. create local semaphores for sync sender_master & childs
void init_local_group_sems() {
    struct Semaphore* tmp = gen_semaphores_local(1, __FILE__);
    master_chid_sem = *tmp;
    free(tmp);

    logger("[main] [init] master_chid_sem.sem_id = %d", master_chid_sem.sem_id);
}

static void sender_get_pids_thread(int reader_pid) {
    int sfd = init_signals_for_reader();

    logger("[main] Send SIGUSR2 to reader_pid = %d", reader_pid);

    // send main thread request for getting another pid
    send_int_via_signal(0, reader_pid, SIGUSR2);

    // setup configs
    struct Sender_child_cfg* cfg = (struct Sender_child_cfg*) buf_for_cfgs.ptr;
    for(int i = 0; i < N_CHILDS; i++) {
        struct signalfd_siginfo fdsi;
        read(sfd, &fdsi, sizeof(struct signalfd_siginfo));
        logger("[main] Readed");
        cfg[i].pid_to_send = fdsi.ssi_int;
        cfg[i].cur_buf_idx = 0;
        say_we_ready(reader_pid);
    }

    // cleanup
    close(sfd);
    sigset_t mask;
    sigemptyset(&mask);
    if (sigprocmask(SIG_BLOCK, &mask, NULL) == -1)
        handle_error("sigprocmask");
}

static volatile int ignore_timeout = 0;

static void timer_header(int sig) {
    if(!ignore_timeout)
        kill(getpid(), SIGINT);
}

// 3. create shared buf_for_cgfs
void init_buf_for_cfg(int reader_pid) {
    buf_for_cfgs = shared_calloc_local(sizeof(struct Sender_child_cfg) * N_CHILDS, __FILE__);

    #if 1
    struct sigaction act;
    act.sa_handler = &timer_header;
    if(sigaction(SIGALRM, &act, NULL) < 0)
        handle_error("Cant set SIGALRM handler");
    alarm(3);

    sender_get_pids_thread(reader_pid);
    ignore_timeout = 1;

    struct Sender_child_cfg* cfg = (struct Sender_child_cfg*) buf_for_cfgs.ptr;
    FOREACH_CHILD(i) {
        logger("[main] [dump] cfg[%d].pid = %d", i, cfg[i].pid_to_send);
        logger("[main] [dump] cfg[%d].bid = %d", i, cfg[i].cur_buf_idx);
    }

    #else
    struct Sender_child_cfg* cfg = (struct Sender_child_cfg*) buf_for_cfgs.ptr;
    char filename[128];
    sprintf(filename, "/proc/%d/stack", reader_pid);
    shm_for_pids = shared_calloc_key(sizeof(pid_t) * N_CHILDS + sizeof(int), ftok(filename,0));
    printf("Try to lock\n");
    lock_shm(shm_for_pids);
    int* ibuf_ptr = shm_for_pids.ptr;
    if(ibuf_ptr[0] != shm_for_pids.sync)
        exit(EXIT_FAILURE);
    ibuf_ptr++;
    printf("lock!!\n");
    for(int i = 0; i < N_CHILDS; i++) {
        cfg[i].pid_to_send = ibuf_ptr[i];
        cfg[i].cur_buf_idx = 0;
    }
    #endif

    //shared_free(shm_for_pids);
}

// 4. init swap_buffers
void init_swap_buffers() {
    for(int i = 0; i < N_SWAP_BUFFERS; i++)
        swap_buffer[i] = get_swap_buff(SWAP_BUF_SIZE, __FILE__);
}

// Union of 1-4 init stages
void init(int reader_pid) {
    reader_process_pid = reader_pid;
    default_setting_up();
    init_signal_ranges();
    init_buf_for_cfg(reader_pid);
    init_inter_process_sems(reader_pid);
    init_local_group_sems();
    init_swap_buffers();
}

/*
    ===============================================================================================
*/

void full_cleanup() {
    // delete swap-buffers
    for(int i = 0; i < N_SWAP_BUFFERS; i++)
        cleanup_swap(swap_buffer[i]);
    // delete local-semaphores
    semctl(master_chid_sem.sem_id, 1, IPC_RMID);
    // delete shared-mem for configs
    shared_free(buf_for_cfgs);
}

/*
    ===============================================================================================
*/

/*
    \brief Function for another process, just read file into swap-buffers
*/
void reader_thread(int fd) {
    int buf_idx = 0;
    int readed = 0;

    for(int i = 0; i < N_SWAP_BUFFERS; i++)
        set_active_bytes(swap_buffer[i], 1);

    do{
        logger("[reader_thread] Wait for free buffer");
        Z(swap_buffer[buf_idx].sem);
        A(swap_buffer[buf_idx].sem, 1);
        logger("[reader_thread] Find free buffer");

        logger("[reader_thread] Read data into shared mem");
        readed = read(fd, swap_buffer[buf_idx].shm.ptr, SWAP_BUF_SIZE);
        set_active_bytes(swap_buffer[buf_idx], readed);
        logger("[reader_thread] Successfully writed %d bytes in shm", readed);
        A(swap_buffer[buf_idx].sem, 2);

        buf_idx ++;
        buf_idx %= N_SWAP_BUFFERS;
    }while(readed > 0);
    close(fd);

    logger("[reader_thread] End of reading file");
}

/*
    \brief  Function for minor process of sender
    \detail Each of minor process communicate with child of master-reader
            process, and transfer data via signals
*/
void work_for_child(int internal_index) {
    int sfd = init_signals_for_sender();

    char* buff__ = 0;
    struct Sender_child_cfg* cfgs = (struct Sender_child_cfg*)buf_for_cfgs.ptr;
    cfgs = &cfgs[internal_index];

    // Say sender-master that we are ready for reading signals
    A(master_chid_sem, 1);
    struct signalfd_siginfo fdsi;
    while(1) {
        int r_ = read(sfd, &fdsi, sizeof(struct signalfd_siginfo));
        if(r_ != sizeof(struct signalfd_siginfo))
            handle_error("Problems with reading signal from sfd");

        if(fdsi.ssi_signo == SIGUSR2) {
            // sets actual buffer ptr
            buff__ = swap_buffer[cfgs->cur_buf_idx].shm.ptr;
            buff__ += BUFFER_SIZE * internal_index;

            logger("[child] getted SIGUSR2");    
            logger("[child] n_bytes = %d", cfgs->n_bytes_to_send);
            logger("[child] pid = %d", cfgs->pid_to_send);
            logger("[child] swp_id = %d", cfgs->cur_buf_idx);

            // send buffer via signals
            send_buffer_via_signal(
                cfgs->n_bytes_to_send,
                cfgs->pid_to_send,
                buff__
            );

            logger("[child] sucessful sended data");
            A(master_chid_sem, 1);
            logger("[child] A_local(1)");
        } else if(fdsi.ssi_signo == SIGQUIT) {
            logger("[child] getted SIGQUIT"); 
            union sigval val;
            val.sival_int = 0;
            sigqueue(cfgs->pid_to_send, SIGUSR2, val);
            break;
        } else {
            logger("[child] [error] getted UNDEFINED SIGNAL");
            exit(EXIT_FAILURE);
        }
    }
}

/*
    ===============================================================================================
*/


/*
    \brief Helper function for sending entire swap_buffer via signal
*/
void send_swap_buffer(int swp_buf_idx, int readed_bytes) {
    // rewrite config for each child
    int n_bytes = readed_bytes;
    struct Sender_child_cfg* cfgs = (struct Sender_child_cfg*)buf_for_cfgs.ptr;
    FOREACH_CHILD(i) {
        if(n_bytes >= BUFFER_SIZE) {
            cfgs[i].n_bytes_to_send = BUFFER_SIZE;
            n_bytes -= BUFFER_SIZE;
        } else {
            cfgs[i].n_bytes_to_send = n_bytes;
            n_bytes = 0;
        }
        cfgs[i].cur_buf_idx = swp_buf_idx;
    }

    // send signal to our child
    FOREACH_CHILD(i)
        kill(child_pids[i], SIGUSR2);

    // wait untial all out childs are ready
    D(master_chid_sem, N_CHILDS);
}


int swp_buf_idx____ = 0;
int readed_bytes___ = 0;
void main_loop() {
    int readed_bytes = 0;
    int swp_buf_idx = swp_buf_idx____;

    logger("[main] Wait until reader_thread is writed data");
    D(swap_buffer[swp_buf_idx].sem, 2);
    logger("[main] reader_thread successfuly readed data");

    readed_bytes = get_active_bytes(swap_buffer[swp_buf_idx]);
    logger("[main] in shm_mem writed %d bytes", readed_bytes);
    readed_bytes___ = readed_bytes;
    if(!readed_bytes) return;

    send_swap_buffer(swp_buf_idx, readed_bytes);

    // mark current swap_buffer as ready (0 -- ready)
    D(swap_buffer[swp_buf_idx].sem, 1);

    // move to the next buffer
    swp_buf_idx ++;
    swp_buf_idx %= N_SWAP_BUFFERS;

    swp_buf_idx____ = swp_buf_idx;
}

/*
    \brief SIGINT Handler
    \detai For correct execution kill all minor-process
*/
static void kill_all(int sig) {
    FOREACH_CHILD(i)
        kill(child_pids[i], SIGKILL);
    kill(reader_thread_pid, SIGKILL);
    full_cleanup();
    // also interupt reader
    kill(reader_process_pid, SIGINT);
    exit(EXIT_FAILURE);
}

void set_sigint_handler() {
    struct sigaction act;
    act.sa_handler = &kill_all;    
    act.sa_flags = SA_RESTART; 
    if(sigaction(SIGINT, &act, NULL) < 0)
        handle_error("Cant set SIGINT handler");
}

void sub_process_creation(int fd) {
    // read file in another thread
    pid_t pid = 0;
    reader_thread_pid = RUN_IN_FG(reader_thread(fd));

    // create childs
    FOREACH_CHILD(i) {
        child_pids[i] = RUN_IN_FG(work_for_child(i));
    }
    // wait unil childs is not ready to read signals
    D(master_chid_sem, N_CHILDS);
}

static void inline is_process_exist(pid_t pid) {
    if(getpgid(pid) < 0) {
        printf("Process with %d pid doesnt exist\n", pid);
        exit(EXIT_FAILURE);
    }
}

int main(int argc, char** argv) {
    if(argc != 3) {
        printf("Use first param: pid of reader\n"
               "And the second: input filename\n");
        return 1;
    }
    int reader_pid = atoi(argv[1]);
    is_process_exist(reader_pid);

    int fd = open(argv[2], O_RDONLY, 0666);
    if(fd < 0)
        handle_error("Cant open input file for reading.");

    init(reader_pid);
    sub_process_creation(fd);
    set_sigint_handler();
    
    sender_chain.action = main_loop;
    do{
        chain_interation(&sender_chain);
    }while(readed_bytes___);

    // send SIGQUIT for stop childs
    FOREACH_CHILD(i) {
        union sigval val;
        val.sival_int = 0;
        sigqueue(child_pids[i], SIGQUIT, val);
    }

    // TODO: cleanup ???
    for(int i = 0; i < N_CHILDS + 1;  i++) {
        int wstatus = 0;
        pid_t pid = wait(&wstatus);
        if(WIFEXITED(wstatus) == 1) {
            int ret_code = WEXITSTATUS(wstatus);
            logger("[main] ret_code[%d] = %d", i, ret_code);
        }
    }

    full_cleanup();

    shared_free(shm_for_pids);

    return 0;    
}
