#include "libs/transfer.h"
#include "libs/dbg.h"
#include <sys/signalfd.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>

// for project constants
#include "config.inc"

static int START_SIGNAL__ = 0;
static int END_SIGNAL__ = 0;
static int SIGNAL_RANGE__ = 0;

// ================================================================================================

void wait_until_readyp(pid_t pid) {
    static sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGURG);
    siginfo_t info;
    int attempts = 100;
    do{
        if(sigwaitinfo(&set, &info) < 0) handle_error("sigwaitinfo");
        attempts--;
    }while(info.si_pid != pid && attempts);

    if(attempts == 0)
        handle_error("Too many processes sending signals");
}

// wait until something don't tell us that he is ready ...
void wait_until_ready() {
    static sigset_t set;
    int sig;
    int *sigptr = &sig;
    sigemptyset(&set);
    sigaddset(&set, SIGURG);
    if(sigwait(&set, sigptr) < 0) handle_error("sigwait");
    if(sig != SIGURG) handle_error("invalid signal");
}

// say to pid that we are ready
void say_we_ready(pid_t pid) {
    // according to man this signal is ignoring by default
    kill(pid, SIGURG);
}

// ================================================================================================

void send_int_via_signal(Sended_t data, pid_t pid, int sig) {
    union sigval val;
    val.sival_ptr = (Sended_t)data;
    sigqueue(pid, sig, val);
}


// send a pack of int via signal
static void send_arr_via_signal(Sended_t* ibuf_ptr, pid_t pid) {
    for(int i = 0; i < SIGNAL_RANGE__; i++) {
        send_int_via_signal(ibuf_ptr[i], pid, START_SIGNAL__ + i);
    }
}

void send_buffer_via_signal(int n_bytes, pid_t pid, char* buff) {
    send_int_via_signal(n_bytes, pid, SIGUSR1);
    Sended_t* ibuf_ptr = (Sended_t*)buff;
    for(int i = 0; i < BUFFER_SIZE / SIGNAL_RANGE__ / sizeof(Sended_t); i++) {
        wait_until_ready(); // sync via signals
        send_arr_via_signal(&ibuf_ptr[i * SIGNAL_RANGE__], pid);
        say_we_ready(pid); // sync via signals
    }

    // wait until reader is ready
    wait_until_ready(); // sync via signals
}

// ================================================================================================

static void read_arr_via_signal(Sended_t* ibuf_ptr, int sfd) {
    int r_ = 0;
    struct signalfd_siginfo fdsi;
    for(int i = 0; i < SIGNAL_RANGE__; i++) {
        r_ = read(sfd, &fdsi, sizeof(struct signalfd_siginfo));
        if(r_ < 0) handle_error("read from sfd");
        ibuf_ptr[i] = (Sended_t)fdsi.ssi_ptr;
    }
}

void read_buff_via_signal(int sfd, char* buffer, pid_t sender_pid) {
    say_we_ready(sender_pid); // sync via signals

    Sended_t* ibuf_ptr = (Sended_t*)buffer;
    for(int i = 0; i < BUFFER_SIZE / SIGNAL_RANGE__ / sizeof(Sended_t); i++) {
        wait_until_ready(); // sync via signals
        read_arr_via_signal(&ibuf_ptr[i * SIGNAL_RANGE__], sfd);
        say_we_ready(sender_pid); // sync via signals
    }
}

// ================================================================================================


int init_signals_for_reader() {
    if(START_SIGNAL__ == 0) {
        printf("You should call init_signal_ranges(), then init_signals_for_reader()\n");
        exit(EXIT_FAILURE);
    }

    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGQUIT);
    sigaddset(&mask, SIGUSR1);
    sigaddset(&mask, SIGUSR2);
    for(int sig_ = 0; sig_ < SIGNAL_RANGE__; sig_++)
        sigaddset(&mask, START_SIGNAL__ + sig_);

    int sfd = signalfd(-1, &mask, 0);
    if (sfd == -1)
        handle_error("signalfd");

    sigaddset(&mask, SIGURG); 
    if (sigprocmask(SIG_BLOCK, &mask, NULL) == -1)
        handle_error("sigprocmask");

    return sfd;
}

int init_signals_for_sender() {
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGQUIT);
    sigaddset(&mask, SIGUSR2); 
    
    int sfd = signalfd(-1, &mask, 0);
    if (sfd == -1)
        handle_error("signalfd");

    sigaddset(&mask, SIGURG); 
    if (sigprocmask(SIG_BLOCK, &mask, NULL) == -1)
        handle_error("sigprocmask");

    return sfd;
}

void init_signal_ranges() {
    START_SIGNAL__ = SIGRTMIN;
    END_SIGNAL__ = SIGRTMAX;
    SIGNAL_RANGE__ = END_SIGNAL__ - START_SIGNAL__;
}
