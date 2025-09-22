#include <fcntl.h>
#include <unistd.h>

int main() {
    int fd = open("/dev/console", 1);
    write(fd, "\033[2J\033[H", 7);  // Clear screen and move cursor to home
    write(fd, "Hello World", 11);
    
    for (;;) pause();
}
