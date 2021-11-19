#ifndef TRANSFER_H
#define TRANSFER_H
#include <sys/types.h>

typedef size_t Sended_t;
#define BUFFER_SIZE 30*sizeof(Sended_t)*8

void wait_until_ready();
void wait_until_readyp(pid_t pid);
void say_we_ready(pid_t pid);

void send_int_via_signal(Sended_t data, pid_t pid, int sig);
void send_buffer_via_signal(int n_bytes, pid_t pid, char* buff);
void read_buff_via_signal(int sfd, char* buffer, pid_t sender_pid);
void init_signal_ranges();
int init_signals_for_reader();
int init_signals_for_sender();

#endif