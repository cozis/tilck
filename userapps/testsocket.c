#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>

int main(void)
{
    int ret;

    ret = socket(AF_INET, SOCK_DGRAM, 0);
    printf("socket created (%d)\n", ret);
    int fd = ret;

    ret = close(fd);
    printf("socket closed (%d)\n", ret);
    return 0;
}