#include <unistd.h>

int main(void)
{
    if (getuid() || geteuid() || write(4, "1", 1) != 1) return 126;
    close(4);
    for (;;) pause();
}
