#include <kernel/userspace.h>

static bool install_boot_programs(void);

/* Mount setup is shared by normal startup and explicit regression boots. */
bool arm64_filesystems_initialize(void) {
    vfs_init();

    inode_t *root = tmpfs_create_root();

    if (!root) {
        return false;
    }

    errno_t status = vfs_mount("/", root);
    inode_unref(root);

    if (status < 0) {
        return false;
    }

    devfs_init();

    return vfs_mkdir(&kernel_process, "/tmp", 0755) == EOK && install_boot_programs();
}

extern const unsigned char arm64_init_image[], arm64_init_image_end[];
#ifndef KERNEL_SELF_TESTS
extern const unsigned char arm64_busybox_image[], arm64_busybox_image_end[];
#endif

static bool install_program(const char *path, const unsigned char *image, size_t length) {
    int descriptor;

    if (vfs_open(&kernel_process, path, O_WRONLY | O_CREAT | O_EXCL, 0755, &descriptor) < 0) {
        return false;
    }

    size_t completed = 0;

    while (completed < length) {
        size_t bytes = 0;
        errno_t status =
            vfs_write(&kernel_process, descriptor, image + completed, length - completed, &bytes);
        if (status < 0 || !bytes) {
            vfs_close(&kernel_process, descriptor);

            return false;
        }

        completed += bytes;
    }

    return vfs_close(&kernel_process, descriptor) == EOK;
}

/* Seed the memory-backed boot filesystem with ordinary executable files.
 * Embedded images are only the transport; all subsequent exec uses VFS paths. */
static bool install_boot_programs(void) {
    if (vfs_mkdir(&kernel_process, "/sbin", 0755) < 0 ||
        vfs_mkdir(&kernel_process, "/bin", 0755) < 0 ||
        !install_program("/sbin/init", arm64_init_image, arm64_init_image_end - arm64_init_image)) {
        return false;
    }
#ifndef KERNEL_SELF_TESTS
    const char *paths[] = {"/bin/busybox", "/bin/sh", "/bin/echo", "/bin/cat", "/bin/ls",
        "/bin/pwd", "/bin/true", "/bin/false", "/bin/mkdir", "/bin/rm", "/bin/wc", "/bin/env",
        "/bin/uname"};
    for (size_t index = 0; index < sizeof(paths) / sizeof(paths[0]); ++index) {
        if (!install_program(
                paths[index], arm64_busybox_image, arm64_busybox_image_end - arm64_busybox_image)) {
            return false;
        }
    }
#endif
    return true;
}
