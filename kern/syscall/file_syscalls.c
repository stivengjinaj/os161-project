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
    struct proc *proc;
    struct openfile *of;
    struct iovec iov;
    struct uio kuio;
    int result;
    char *kbuf;

    /* Validate file descriptor */
    if (fd < 0 || fd >= OPEN_MAX) {
        return EBADF;
    }

    /* Validate buffer pointer */
    if (buf_ptr == NULL) {
        return EFAULT;
    }

    /* Handle stdout/stderr specially */
    if (fd == STDOUT_FILENO || fd == STDERR_FILENO) {
        /* Allocate kernel buffer */
        kbuf = kmalloc(size);
        if (kbuf == NULL) {
            return ENOMEM;
        }
        
        /* Copy from user space to kernel space */
        result = copyin(buf_ptr, kbuf, size);
        if (result) {
            kfree(kbuf);
            return result;
        }
        
        /* Write to console */
        for (size_t i = 0; i < size; i++) {
            putch(kbuf[i]);
        }
        
        kfree(kbuf);
        return size;
    }

    proc = curproc;
    KASSERT(proc != NULL);

    /* Get openfile structure */
    of = proc->fileTable[fd];
    if (of == NULL) {
        return EBADF;
    }

    /* Check if file is open for writing */
    if ((of->mode & O_ACCMODE) == O_RDONLY) {
        return EBADF;
    }

    /* Allocate kernel buffer */
    kbuf = kmalloc(size);
    if (kbuf == NULL) {
        return ENOMEM;
    }

    /* Copy data from user space to kernel space */
    result = copyin(buf_ptr, kbuf, size);
    if (result) {
        kfree(kbuf);
        return result;
    }

    /* Acquire lock for thread-safe access */
    lock_acquire(of->lock);

    /* Set up uio structure for the write operation */
    uio_kinit(&iov, &kuio, kbuf, size, of->offset, UIO_WRITE);

    /* Perform the write */
    result = VOP_WRITE(of->vn, &kuio);
    if (result) {
        lock_release(of->lock);
        kfree(kbuf);
        return result;
    }

    /* Update the file offset */
    of->offset = kuio.uio_offset;

    lock_release(of->lock);

    /* Calculate bytes written */
    int bytes_written = size - kuio.uio_resid;
    
    kfree(kbuf);

    /* Return the number of bytes written */
    return bytes_written;
}

int
sys_read(int fd, userptr_t buf_ptr, size_t size)
{
    struct proc *proc;
    struct openfile *of;
    struct iovec iov;
    struct uio kuio;
    int result;
    char *kbuf;

    /* Validate file descriptor */
    if (fd < 0 || fd >= OPEN_MAX) {
        return EBADF;
    }

    /* Validate buffer pointer */
    if (buf_ptr == NULL) {
        return EFAULT;
    }

    proc = curproc;
    KASSERT(proc != NULL);

    /* Get the openfile structure */
    of = proc->fileTable[fd];
    if (of == NULL) {
        /* Handle stdin specially if not in file table */
        if (fd == STDIN_FILENO) {
            kbuf = kmalloc(size);
            if (kbuf == NULL) {
                return ENOMEM;
            }
            
            for (size_t i = 0; i < size; i++) {
                int ch = getch();
                if (ch < 0) {
                    result = copyout(kbuf, buf_ptr, i);
                    kfree(kbuf);
                    if (result) {
                        return result;
                    }
                    return i;
                }
                kbuf[i] = ch;
            }
            
            result = copyout(kbuf, buf_ptr, size);
            kfree(kbuf);
            if (result) {
                return result;
            }
            return size;
        }
        return EBADF;
    }

    /* Check if file is open for reading */
    if ((of->mode & O_ACCMODE) == O_WRONLY) {
        return EBADF;
    }

    /* Allocate kernel buffer */
    kbuf = kmalloc(size);
    if (kbuf == NULL) {
        return ENOMEM;
    }

    /* Acquire lock for thread-safe access */
    lock_acquire(of->lock);

    /* Set up uio structure for the read operation */
    uio_kinit(&iov, &kuio, kbuf, size, of->offset, UIO_READ);

    /* Perform the read */
    result = VOP_READ(of->vn, &kuio);
    if (result) {
        lock_release(of->lock);
        kfree(kbuf);
        return result;
    }

    /* Update the file offset */
    of->offset = kuio.uio_offset;

    /* Calculate bytes read */
    int bytes_read = size - kuio.uio_resid;

    lock_release(of->lock);

    /* Copy data to user space */
    result = copyout(kbuf, buf_ptr, bytes_read);
    if (result) {
        kfree(kbuf);
        return result;
    }

    kfree(kbuf);

    /* Return the number of bytes read */
    return bytes_read;
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
        if (proc->fileTable[i] == NULL) {
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

    proc->fileTable[fd] = of;
    *retval = fd;

    return 0;
}

int
sys_close(int fd)
{
    if (fd < 0 || fd >= OPEN_MAX) return EBADF;

    struct proc *p = curproc;
    struct openfile *of = p->fileTable[fd];
    if (of == NULL) return EBADF;

    p->fileTable[fd] = NULL;

    lock_acquire(of->lock);
    KASSERT(of->count > 0);
    of->count--;
    bool last = (of->count == 0);
    lock_release(of->lock);

    if (last) {
        vfs_close(of->vn);
        lock_destroy(of->lock);
        kfree(of);
    }
    return 0;
}

int sys_dup2(int oldfd, int newfd, int32_t *retval)
{
    /* check validity of oldfd and newfd */
    if (oldfd < 0 || oldfd >= OPEN_MAX || newfd < 0 || newfd >= OPEN_MAX)
        return EBADF;

    /* check if oldfd is open */
    if (curproc->fileTable[oldfd] == NULL)
        return EBADF;

    /* special case:if oldfd is the same as newfd, do nothing */
    if (oldfd == newfd)
    {
        *retval = newfd;
        return 0;
    }

    /* special case: newfd is already open, close it before reusing it */
    if (curproc->fileTable[newfd] != NULL)
    {
        struct openfile *of = curproc->fileTable[newfd];
        lock_acquire(of->lock);
        curproc->fileTable[newfd] = NULL;
        if (--of->count == 0)
        {
            struct vnode *vn = of->vn;
            of->vn = NULL;
            vfs_close(vn);
        }
        lock_release(of->lock);
    }

    /* duplicate the fd: point newfd to the same file object as oldfd
    increase the reference count for the file object */
    lock_acquire(curproc->fileTable[oldfd]->lock);
    curproc->fileTable[newfd] = curproc->fileTable[oldfd];
    curproc->fileTable[newfd]->count++;
    lock_release(curproc->fileTable[oldfd]->lock);

    *retval = newfd;
    return 0;
}

#endif