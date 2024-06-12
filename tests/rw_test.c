#include "stdio.h"
#include "stdlib.h"
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "common.h"


int main(int argc, char const *argv[])
{
    int fd = open(TARGET_FILE_PATH, O_RDWR);
    if (fd < 0) {
        printf("open %s failed\n", TARGET_FILE_PATH);
        return -1;
    }

    char *wbuf = (char *)malloc(DEFAULT_IO_SIZE);
    char *rbuf = (char *)malloc(DEFAULT_IO_SIZE);

    write(fd, wbuf, DEFAULT_IO_SIZE);
    
    read(fd, rbuf, DEFAULT_IO_SIZE);

    if (memcmp(wbuf, rbuf, DEFAULT_IO_SIZE) != 0) {
        printf("read/write failed\n");
        return -1;
    }

    printf("Read/Write ok!\n");    
    return 0;
}
