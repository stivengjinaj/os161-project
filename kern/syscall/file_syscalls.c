/*
 * AUthor: G.Cabodi
 * Very simple implementation of sys_read and sys_write.
 * just works (partially) on stdin/stdout
 */

#include <types.h>
#include <kern/unistd.h>
#include <kern/fcntl.h>
#include <kern/errno.h>
#include <copyinout.h>
#include <vfs.h>
#include <current.h>
#include <proc.h>
#include <openfile.h>
#include <uio.h>
#include <vnode.h>
#include <syscall.h>
#include <limits.h>
#include <lib.h>
#include <kern/seek.h>
#include <kern/stat.h>
#include <synch.h>
#include <kern/limits.h>

#if OPT_SHELL
/*
 * simple file system calls for write/read
 */
int
sys_write(int fd, userptr_t buf_ptr, size_t size)
{
  int i;
  char *p = (char *)buf_ptr;

  if (fd!=STDOUT_FILENO && fd!=STDERR_FILENO) {
    kprintf("sys_write supported only to stdout\n");
    return -1;
  }

  for (i=0; i<(int)size; i++) {
    putch(p[i]);
  }

  return (int)size;
}

int
sys_read(int fd, userptr_t buf_ptr, size_t size)
{
  int i;
  char *p = (char *)buf_ptr;

  if (fd!=STDIN_FILENO) {
    kprintf("sys_read supported only to stdin\n");
    return -1;
  }

  for (i=0; i<(int)size; i++) {
    p[i] = getch();
    if (p[i] < 0) 
      return i;
  }

  return (int)size;
}

int
sys_open(userptr_t filename, int flags, mode_t mode, int *retval)
{
    char kpath[PATH_MAX];
    struct vnode *vn;
    struct openfile *of;
    int fd;
    int result;
    size_t actual;

    if (filename == NULL) {
        return EFAULT;
    }

    result = copyinstr(filename, kpath, sizeof(kpath), &actual);
    if (result) {
        return result;
    }

    if (actual == 1) {
        return EINVAL;
    }

    /* Check flag validity */
    int accmode = flags & O_ACCMODE;
    if (accmode != O_RDONLY && accmode != O_WRONLY && accmode != O_RDWR) {
        return EINVAL;
    }

    /* Check for conflicting flags */
    if ((flags & O_APPEND) && (accmode == O_RDONLY)) {
        return EINVAL;
    }

    /* Open the file */
    result = vfs_open(kpath, flags, mode, &vn);
    if (result) {
        return result;
    }

    /* Allocate openfile structure */
    of = kmalloc(sizeof(struct openfile));
    if (of == NULL) {
        vfs_close(vn);
        return ENOMEM;
    }

    /* Initialize openfile structure */
    of->vn = vn;
    of->mode = flags & O_ACCMODE;
    of->count = 1;
    of->offset = 0;

    /* Create lock for the openfile */
    of->lock = lock_create("openfile_lock");
    if (of->lock == NULL) {
        vfs_close(vn);
        kfree(of);
        return ENOMEM;
    }

    /* If O_APPEND flag is set, seek to end of file */
    if (flags & O_APPEND) {
        struct stat statbuf;
        result = VOP_STAT(vn, &statbuf);
        if (result) {
            lock_destroy(of->lock);
            vfs_close(vn);
            kfree(of);
            return result;
        }
        of->offset = statbuf.st_size;
    }

    /* Find an available file descriptor in the process's file table */
    struct proc *proc = curproc;
    KASSERT(proc != NULL);

    /* Look for an empty slot in the file descriptor table */
    fd = -1;
    for (int i = 0; i < OPEN_MAX; i++) {
        if (proc->p_filetable[i] == NULL) {
            fd = i;
            break;
        }
    }

    if (fd == -1) {
        lock_destroy(of->lock);
        vfs_close(vn);
        kfree(of);
        return EMFILE;
    }

    proc->p_filetable[fd] = of;
    *retval = fd;

    return 0;
}

int
sys_close(int fd)
{
    struct proc *proc;
    struct openfile *of;

    /* Validate file descriptor */
    if (fd < 0 || fd >= OPEN_MAX) {
        return EBADF;
    }

    proc = curproc;
    KASSERT(proc != NULL);

    /* Check if file descriptor is open */
    of = proc->p_filetable[fd];
    if (of == NULL) {
        return EBADF;
    }

    /* Remove from process file table */
    proc->p_filetable[fd] = NULL;

    /* Acquire lock and decrement reference count */
    lock_acquire(of->lock);
    KASSERT(of->count > 0);
    of->count--;

    /* If this was the last reference, clean up */
    if (of->count == 0) {
        vfs_close(of->vn);
        lock_release(of->lock);
        lock_destroy(of->lock);
        kfree(of);
    } else {
        lock_release(of->lock);
    }

    return 0;
}
#endif