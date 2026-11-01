/* Start an unmodified static BusyBox shell through the Exec ABI. */
#include <stdio.h>
#include <unistd.h>

extern char **environ;

int main(void) {
    puts("Aether init: ARM64 userspace ready.");
    fflush(stdout);
    char *arguments[] = {"/bin/sh", NULL};
    execve("/bin/sh", arguments, environ);
    perror("execve /bin/sh");
    return 1;
}
