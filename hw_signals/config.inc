#ifndef CONFIG_H
#define CONFIG_H

#define PAGE_SIZE 0x1000
#define N_CHILDS 64
#define N_SWAP_BUFFERS 32
#define SWAP_BUF_SIZE (BUFFER_SIZE * N_CHILDS)

enum {
    READER_READY = 0,
    SENDER_READY = 1
};

#define FOREACH_CHILD(var_) for(int var_ = 0; var_ < N_CHILDS; var_++ )

#endif
