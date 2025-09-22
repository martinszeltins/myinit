#include <fcntl.h>
#include <unistd.h>

int main() {
    int fd = open("/dev/console", 1);
    write(fd, "Hello World", 11);
    
    for (;;) pause();
}
