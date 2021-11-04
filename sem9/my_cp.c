#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define check(code__) \
    do{if(code__ < 0) {perror("Error"); exit(EXIT_FAILURE);}}while(0)

#define BUF_SIZE 0x1000

int get_file_size(const char* filename) {
    struct stat sts;
    check(stat(filename, &sts));
    return sts.st_size;
}

int safe_open(const char* filename, int flags) {
    int fd = open(filename, flags, 0666);
    check(fd);
    return fd;
}

int main(int argc, char** argv) {
    if(argc != 3) {
        printf("set inut and output file");
        return EXIT_FAILURE;
    }

    const char* in_filename = argv[1];
    const char* out_filename = argv[2];

    int in_fd = safe_open(in_filename, O_RDONLY);
    int out_fd = safe_open(out_filename, O_WRONLY | O_CREAT | O_TRUNC);

    int n_bytes_remained = get_file_size(in_filename);
    int off = 0;
    char* src = 0;

    while(n_bytes_remained) {
        int actual_size = n_bytes_remained > BUF_SIZE ? BUF_SIZE : n_bytes_remained;
        src = mmap(0, actual_size, PROT_READ, MAP_SHARED, in_fd, off);
        write(out_fd, src, actual_size);
        munmap(src, actual_size);

        off += actual_size;
        n_bytes_remained -= actual_size;
    }

    close(in_fd);
    close(out_fd);
    
    return 0;
}
