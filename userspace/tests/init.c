/* Static musl init and end-to-end ABI regression. A kernel-created child lets
 * waitpid exercise real completion/reaping, including fork and exec rollback. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <sys/auxv.h>
#include <sys/syscall.h>
#include <sys/utsname.h>
#include <dirent.h>
#include <time.h>
#include <sched.h>
#include <termios.h>

static __thread volatile unsigned long thread_cookie = 0x123456789abcdef0UL;
static unsigned char zero_storage[8192];
#define REQUIRE(condition)                                                                         \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            dprintf(2, "ARM INIT FAIL pid=%d line=%d: %s errno=%d\n", getpid(), __LINE__,          \
                #condition, errno);                                                                \
            _Exit(1);                                                                              \
        }                                                                                          \
    } while (0)

/* A single asm block keeps the compiler from treating v31 as call-clobbered
 * across SVC. Different processes use different values on the same CPU. */
static void check_simd(void) {
    unsigned long expected = (unsigned long)getpid() * 0x101010101010101UL;

    for (unsigned int iteration = 0; iteration < 32; ++iteration) {
        unsigned long observed;
        __asm__ volatile("dup v31.2d, %1\n mov x8, #124\n svc #0\n umov %0, v31.d[1]"
            : "=r"(observed)
            : "r"(expected)
            : "x0", "x8", "v31", "memory");
        REQUIRE(observed == expected);
        REQUIRE(getppid() > 0);
        REQUIRE(thread_cookie == 0x123456789abcdef0UL);
    }
}

/* libc allocation, anonymous mapping/protection, BSS and TLS all require the
 * correct ELF program headers and userspace initial stack/auxiliary vector. */
static void check_memory(void) {
    for (size_t index = 0; index < sizeof(zero_storage); ++index) {
        REQUIRE(!zero_storage[index]);
    }

    unsigned char *memory = malloc(1024 * 1024);
    REQUIRE(memory);
    memset(memory, 0x5a, 1024 * 1024);
    REQUIRE(memory[1024 * 1024 - 1] == 0x5a);
    free(memory);
    memory = mmap(NULL, 12288, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    REQUIRE(memory != MAP_FAILED && !memory[8192]);
    memory[4096] = 0x7b;
    REQUIRE(mprotect(memory + 4096, 4096, PROT_NONE) == 0);
    REQUIRE(mprotect(memory + 4096, 4096, PROT_READ) == 0 && memory[4096] == 0x7b);
    REQUIRE(mmap(memory, 4096, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1,
                0) == MAP_FAILED &&
            errno == EEXIST);
    REQUIRE(munmap(memory, 12288) == 0);
    memory = mmap(NULL, 8192, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    REQUIRE(memory != MAP_FAILED && munmap(memory + 4096, 4096) == 0);
    memory[4095] = 'x';
    REQUIRE(syscall(SYS_openat, AT_FDCWD, memory + 4095, O_RDONLY, 0) == -1 && errno == EFAULT);
    REQUIRE(munmap(memory, 4096) == 0);
}

/* Exercise the actual shared tmpfs/devfs/VFS, including AArch64 stat layout,
 * vector/positioned I/O, descriptor flags, directories, relative paths and EOF. */
static void check_files(void) {
    char directory[80], pathname[120], renamed_path[120], buffer[256];
    snprintf(directory, sizeof(directory), "/tmp/init-%d", getpid());
    snprintf(pathname, sizeof(pathname), "%s/data", directory);
    snprintf(renamed_path, sizeof(renamed_path), "%s/renamed", directory);
    REQUIRE(mkdir(directory, 0755) == 0);

    int descriptor = open(pathname, O_CREAT | O_RDWR | O_CLOEXEC, 0640);
    REQUIRE(descriptor >= 0);

    struct iovec vectors[] = {{"ARM ", 4}, {"musl", 4}};
    REQUIRE(writev(descriptor, vectors, 2) == 8);
    REQUIRE(lseek(descriptor, 0, SEEK_SET) == 0);
    vectors[0].iov_base = buffer;
    vectors[1].iov_base = buffer + 4;
    REQUIRE(readv(descriptor, vectors, 2) == 8 && !memcmp(buffer, "ARM musl", 8));
    REQUIRE(pread(descriptor, buffer, 4, 0) == 4 && !memcmp(buffer, "ARM ", 4));
    REQUIRE(lseek(descriptor, 0, SEEK_CUR) == 8);
    REQUIRE(pwrite(descriptor, "M", 1, 4) == 1 && lseek(descriptor, 0, SEEK_CUR) == 8);

    struct stat metadata;
    REQUIRE(fstat(descriptor, &metadata) == 0 && metadata.st_size == 8 &&
            (metadata.st_mode & 0777) == 0640);
    REQUIRE(stat(pathname, &metadata) == 0 && metadata.st_size == 8);

    int duplicate = dup(descriptor);
    REQUIRE(duplicate >= 0 && fcntl(duplicate, F_GETFD) == 0);
    REQUIRE(dup3(descriptor, 64, O_CLOEXEC) == 64 && fcntl(64, F_GETFD) == FD_CLOEXEC);
    REQUIRE(close(64) == 0 && close(duplicate) == 0);
    REQUIRE(ftruncate(descriptor, 5000) == 0 && pread(descriptor, buffer, 16, 4096) == 16);

    for (unsigned int index = 0; index < 16; ++index) {
        REQUIRE(!buffer[index]);
    }

    REQUIRE(close(descriptor) == 0);
    DIR *stream = opendir(directory);
    REQUIRE(stream);

    int found = 0;
    struct dirent *entry;

    while ((entry = readdir(stream))) {
        if (!strcmp(entry->d_name, "data")) {
            found = 1;
        }
    }

    REQUIRE(found && closedir(stream) == 0);
    REQUIRE(rename(pathname, pathname) == 0);
    REQUIRE(rename(pathname, "/dev/forbidden") == -1 && errno == EXDEV);
    REQUIRE(rename(pathname, renamed_path) == 0);
    REQUIRE(chdir(directory) == 0 && getcwd(buffer, sizeof(buffer)) && !strcmp(buffer, directory));
    REQUIRE(access("renamed", F_OK) == 0 && chdir("/") == 0);
    REQUIRE(unlink(renamed_path) == 0 && rmdir(directory) == 0);
    descriptor = open("/dev/zero", O_RDONLY);
    REQUIRE(descriptor >= 0 && read(descriptor, buffer, sizeof(buffer)) == sizeof(buffer));

    for (size_t index = 0; index < sizeof(buffer); ++index) {
        REQUIRE(!buffer[index]);
    }

    REQUIRE(close(descriptor) == 0);

    int endpoints[2];
    REQUIRE(pipe2(endpoints, O_CLOEXEC | O_NONBLOCK) == 0);
    REQUIRE(read(endpoints[0], buffer, 1) == -1 && errno == EAGAIN);
    REQUIRE(write(endpoints[1], "pipe", 4) == 4 && read(endpoints[0], buffer, 4) == 4 &&
            !memcmp(buffer, "pipe", 4));
    REQUIRE(
        close(endpoints[1]) == 0 && read(endpoints[0], buffer, 1) == 0 && close(endpoints[0]) == 0);

    /* Neither invalid pointers nor RX output buffers may fault the kernel. */
    REQUIRE(syscall(SYS_write, 1, (void *)0x40080000UL, 1) == -1 && errno == EFAULT);
    REQUIRE(syscall(SYS_openat, AT_FDCWD, (void *)1, O_RDONLY, 0) == -1 && errno == EFAULT);
    REQUIRE(syscall(SYS_read, 0, (const char *)check_files, 1) == -1 && errno == EFAULT);
}

/* Parent waits for the kernel-launched child and consumes its status once. */
int main(int argument_count, char **arguments) {
    if (argument_count == 3 && !strcmp(arguments[1], "exec-child")) {
        REQUIRE(getpid() == atoi(arguments[2]));
        REQUIRE(getenv("EXEC_MARKER") && !strcmp(getenv("EXEC_MARKER"), "preserved"));
        REQUIRE(fcntl(11, F_GETFD) == -1 && errno == EBADF);
        REQUIRE(fcntl(12, F_GETFD) >= 0);

        return 42;
    }

    REQUIRE(argument_count == 2 && !strcmp(arguments[0], "/sbin/init"));
    REQUIRE(getenv("AETHER_ARCH") && !strcmp(getenv("AETHER_ARCH"), "aarch64"));
    REQUIRE(getauxval(AT_PAGESZ) == 4096 && getauxval(AT_PHNUM) && getauxval(AT_RANDOM));
    REQUIRE(thread_cookie == 0x123456789abcdef0UL);
    check_simd();

    if (!strcmp(arguments[1], "child")) {
        return 37;
    }

    if (!strcmp(arguments[1], "orphan")) {
        return 19;
    }

    if (!strcmp(arguments[1], "fault")) {
        *(volatile unsigned long *)0x40080000UL = 1;

        return 99;
    }

    REQUIRE(!strcmp(arguments[1], "parent"));
    check_memory();
    check_files();

    /* Eager fork keeps user memory/TLS private and descriptors shared. Exec
     * preserves PID, passes arbitrary vectors, and closes only CLOEXEC slots. */
    int executable = open("/sbin/init", O_RDONLY);
    REQUIRE(executable >= 0);
    REQUIRE(dup3(executable, 11, O_CLOEXEC) == 11);
    REQUIRE(dup3(executable, 12, 0) == 12);
    REQUIRE(close(executable) == 0);
    char invalid_path[80];
    snprintf(invalid_path, sizeof(invalid_path), "/tmp/invalid-elf-%d", getpid());
    int invalid = open(invalid_path, O_WRONLY | O_CREAT | O_EXCL, 0755);
    REQUIRE(invalid >= 0 && write(invalid, "bad", 3) == 3 && close(invalid) == 0);
    volatile int private_value = 17;
    pid_t launched = fork();
    REQUIRE(launched >= 0);

    if (!launched) {
        REQUIRE(thread_cookie == 0x123456789abcdef0UL);
        check_simd();
        private_value = 99;
        thread_cookie = 7;
        char identifier[32];
        snprintf(identifier, sizeof(identifier), "%d", getpid());
        char *args[] = {"/sbin/init", "exec-child", identifier, NULL};
        char *env[] = {"EXEC_MARKER=preserved", NULL};
        REQUIRE(execve("/does-not-exist", args, env) == -1 && errno == ENOENT);
        REQUIRE(execve(invalid_path, args, env) == -1 && errno == ENOEXEC);
        REQUIRE(execve("/sbin/init", (char *const *)1, env) == -1 && errno == EFAULT);

        /* Exec does not need a free user descriptor slot. */
        while (fcntl(12, F_DUPFD_CLOEXEC, 0) >= 0) {
        }

        REQUIRE(errno == EMFILE);
        execve("/sbin/init", args, env);
        _Exit(98);
    }

    int launched_status;
    REQUIRE(waitpid(launched, &launched_status, 0) == launched);
    REQUIRE(WIFEXITED(launched_status) && WEXITSTATUS(launched_status) == 42);
    REQUIRE(private_value == 17 && thread_cookie == 0x123456789abcdef0UL);
    REQUIRE(close(11) == 0 && close(12) == 0);
    REQUIRE(unlink(invalid_path) == 0);

    /* Termios belongs to the terminal, so serialize mode-changing tests across
     * independently running parents using atomic O_EXCL creation. */
    int terminal_lock;
    while ((terminal_lock =
                   open("/tmp/terminal-settings-lock", O_CREAT | O_EXCL | O_WRONLY, 0600)) < 0) {
        REQUIRE(errno == EEXIST);
        sched_yield();
    }

    int terminal = open("/dev/tty", O_RDONLY | O_NONBLOCK);
    REQUIRE(terminal >= 0);
    struct termios original_terminal, raw_terminal;
    REQUIRE(tcgetattr(terminal, &original_terminal) == 0);
    raw_terminal = original_terminal;
    cfmakeraw(&raw_terminal);
    REQUIRE(tcsetattr(terminal, TCSANOW, &raw_terminal) == 0);
    struct termios observed_terminal;
    REQUIRE(tcgetattr(terminal, &observed_terminal) == 0);
    REQUIRE(!(observed_terminal.c_lflag & (ICANON | ECHO)));
    REQUIRE(tcsetattr(terminal, TCSAFLUSH, &original_terminal) == 0);

    char input_byte;
    REQUIRE(read(terminal, &input_byte, 0) == 0);
    REQUIRE(read(terminal, &input_byte, 1) == -1 && errno == EAGAIN);
    REQUIRE(close(terminal) == 0);
    REQUIRE(close(terminal_lock) == 0 && unlink("/tmp/terminal-settings-lock") == 0);

    struct timespec first_time, second_time, pause = {0, 1000000};
    REQUIRE(clock_gettime(CLOCK_MONOTONIC, &first_time) == 0 && nanosleep(&pause, NULL) == 0);
    REQUIRE(clock_gettime(CLOCK_MONOTONIC, &second_time) == 0);
    REQUIRE(second_time.tv_sec > first_time.tv_sec || second_time.tv_nsec >= first_time.tv_nsec);

    struct utsname identity;
    REQUIRE(uname(&identity) == 0 && !strcmp(identity.machine, "aarch64"));
    REQUIRE(!strcmp(identity.sysname, "Aether") && !strcmp(identity.nodename, "Aether"));
    REQUIRE(!strncmp(identity.release, "0.1-", 4));
    REQUIRE(strlen(identity.release) == 37 && identity.release[16] == '-');
    REQUIRE(!strcmp(identity.release + 17, identity.version));

    int status;
    REQUIRE(waitpid(-1, &status, 0) > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 37);
    REQUIRE(waitpid(-1, &status, WNOHANG) == -1 && errno == ECHILD);

    /* Keep each regression message and its newline in one console write so
     * simultaneous test processes cannot splice lines between writev elements. */
    static const char success[] =
        "ARM musl init: files, pipes, memory, TLS, SIMD and wait passed\n";
    REQUIRE(write(1, success, sizeof(success) - 1) == (ssize_t)(sizeof(success) - 1));

    return 0;
}
