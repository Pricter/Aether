#include <kernel/userspace.h>

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

    return vfs_mkdir(&kernel_process, "/tmp", 0755) == EOK;
}
