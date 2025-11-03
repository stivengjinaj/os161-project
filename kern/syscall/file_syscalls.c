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
ssize_t sys_write(int fd, const void *buf, size_t buflen, int32_t *retval)
{
    /* CHECKING FILE DESCRIPTOR */
    if (fd < 0 || fd >= OPEN_MAX)
        return EBADF;

    /* CHECKING CURPROC AND FILETABLE */
    if (curproc == NULL || curproc->fileTable == NULL)
        return EFAULT;

    /* CHECKING FILE DESCRIPTOR VALIDITY */
    if (curproc->fileTable[fd] == NULL)
        return EBADF;

    struct openfile *of = curproc->fileTable[fd];

    /* ENSURE THE FILE IS NOT READ-ONLY */
    if ((of->mode & O_ACCMODE) == O_RDONLY)
        return EBADF; /* FILE IS NOT OPEN FOR WRITING */

    if (buf == NULL)
        return EFAULT; /* INVALID BUFFER POINTER */

    /* ALLOCATING KERNEL BUFFER */
    char *kbuffer = (char *)kmalloc(buflen);
    if (kbuffer == NULL)
        return ENOMEM;

    /* COPYING DATA FROM USER SPACE TO KERNEL SPACE */
    int err = copyin((const_userptr_t)buf, kbuffer, buflen);
    if (err)
    {
        kfree(kbuffer);
        return EFAULT;
    }

    /* WRITING DATA TO FILE */
    struct iovec iov;
    struct uio kuio;
    struct vnode *vn = of->vn;

    lock_acquire(of->lock);

    uio_kinit(&iov, &kuio, kbuffer, buflen, of->offset, UIO_WRITE);
    err = VOP_WRITE(vn, &kuio);

    /* NO FREE SPACE REMAINING ON THE FILESYSTEM */
    if (err == ENOSPC)
    {
        lock_release(of->lock);
        kfree(kbuffer);
        return ENOSPC;
    }
    /* HARDWARE I/O ERROR OCCURRED */
    if (err == EIO)
    {
        lock_release(of->lock);
        kfree(kbuffer);
        return EIO; 
    }
    if (err)
    {
        lock_release(of->lock);
        kfree(kbuffer);
        return err; /* ERROR DURING FILE WRITE */
    }

    /* UPDATING OFFSET AND RETURN VALUE */
    off_t nbytes = kuio.uio_offset - of->offset;
    *retval = (int32_t)nbytes;
    of->offset = kuio.uio_offset;

    /* RELEASE LOCK AND FREE KERNEL BUFFER */
    lock_release(of->lock);
    kfree(kbuffer);

    return 0;
}

ssize_t sys_read(int fd, const void *buf, size_t buflen, int32_t *retval) {

    /* VALIDATE FILE DESCRIPTOR */
    if (fd < 0 || fd >= OPEN_MAX || curproc->fileTable[fd] == NULL || (curproc->fileTable[fd]->mode & O_ACCMODE) == O_WRONLY) {
        return EBADF;
    }

    /* VALIDATE BUFFER */
    if (buf == NULL) {
        return EFAULT;
    }

    /* ALLOCATE KERNEL BUFFER */
    char *kbuf = kmalloc(buflen);
    if (kbuf == NULL) {
        return ENOMEM;
    }

    /* PREPARE FOR READING */
    struct openfile *of = curproc->fileTable[fd];
    struct iovec iov;
    struct uio kuio;
    uio_kinit(&iov, &kuio, kbuf, buflen, of->offset, UIO_READ);

    /* PERFORM READ OPERATION */
    lock_acquire(of->lock);
    int result = VOP_READ(of->vn, &kuio);
    if (result) {
        lock_release(of->lock);
        kfree(kbuf);
        return result;
    }

    /* UPDATE OFFSET */
    of->offset = kuio.uio_offset;
    *retval = buflen - kuio.uio_resid;

    /* COPY DATA TO USER BUFFER */
    result = copyout(kbuf, (userptr_t)buf, *retval);
    if (result) {
        lock_release(of->lock);
        kfree(kbuf);
        return EFAULT;
    }

    /* CLEAN UP */
    lock_release(of->lock);
    kfree(kbuf);

    return 0;
}

int sys_open(userptr_t filename, int flags, mode_t mode, int *retval)
{
    /* CHECKING IF PATHNAME IS NULL */
    if (filename == NULL)
        return EFAULT;

    /* COPYING PATHNAME FROM USER SPACE TO KERNEL SPACE */
    char *kbuffer = (char *)kmalloc(PATH_MAX * sizeof(char));
    if (kbuffer == NULL)
        return ENOMEM;

    size_t len;
    int err = copyinstr((const_userptr_t)filename, kbuffer, PATH_MAX, &len);
    if (err)
    {
        kfree(kbuffer);
        return EFAULT;
    }

    /* OPENING FILE */
    struct vnode *v;
    err = vfs_open(kbuffer, flags, mode, &v);
    if (err)
    {
        kfree(kbuffer);
        return err;
    }
    kfree(kbuffer);

    /* FIND A FREE FILE DESCRIPTOR (starting from 3, since 0-2 are std streams) */
    int fd = -1;
    for (int i = 3; i < OPEN_MAX; i++)
    {
        if (curproc->fileTable[i] == NULL)
        {
            fd = i;
            break;
        }
    }

    /* CHECK IF FILE TABLE IS FULL */
    if (fd == -1)
    {
        vfs_close(v);
        return EMFILE; /* TOO MANY OPEN FILES IN PROCESS */
    }

    /* CREATE NEW OPENFILE STRUCTURE */
    struct openfile *of = (struct openfile *)kmalloc(sizeof(struct openfile));
    if (of == NULL)
    {
        vfs_close(v);
        return ENOMEM;
    }

    /* INITIALIZE THE OPENFILE STRUCTURE */
    of->vn = v;
    of->count = 1;

    /* CREATE LOCK FOR THIS FILE */
    of->lock = lock_create("FILE_LOCK");
    if (of->lock == NULL)
    {
        vfs_close(v);
        kfree(of);
        return ENOMEM;
    }

    /* SET FILE MODE BASED ON FLAGS */
    switch (flags & O_ACCMODE)
    {
    case O_RDONLY:
        of->mode = O_RDONLY;
        break;
    case O_WRONLY:
        of->mode = O_WRONLY;
        break;
    case O_RDWR:
        of->mode = O_RDWR;
        break;
    default:
        lock_destroy(of->lock);
        vfs_close(v);
        kfree(of);
        return EINVAL;
    }

    /* SET OFFSET BASED ON FLAGS */
    if (flags & O_APPEND)
    {
        /* SET OFFSET TO END OF FILE */
        struct stat filestat;
        err = VOP_STAT(v, &filestat);
        if (err)
        {
            lock_destroy(of->lock);
            vfs_close(v);
            kfree(of);
            return err;
        }
        of->offset = filestat.st_size;
    }
    else
    {
        /* SET OFFSET TO BEGINNING OF FILE */
        of->offset = 0;
    }

    /* ADD TO PROCESS FILE TABLE */
    curproc->fileTable[fd] = of;

    /* RETURN FILE DESCRIPTOR */
    *retval = fd;
    return 0;
}

int sys_close(int fd)
{
    /* CHECK IF THE FILE DESCRIPTOR IS VALID */
    if (fd < 0 || fd >= OPEN_MAX)
        return EBADF;

    /* CHECK IF THE FILE DESCRIPTOR IS IN USE */
    if (curproc->fileTable[fd] == NULL)
        return EBADF;

    /* RETRIEVE THE OPEN FILE STRUCTURE */
    struct openfile *of = curproc->fileTable[fd];
    lock_acquire(of->lock);

    /* REMOVE THE FILE DESCRIPTOR FROM THE PROCESS'S FILE TABLE */
    curproc->fileTable[fd] = NULL;

    /* DECREMENT THE REFERENCE COUNT */
    if (--of->count == 0)
    {
        /* IF NO MORE REFERENCES, CLOSE THE VNODE */
        struct vnode *vn = of->vn;
        of->vn = NULL;
        vfs_close(vn);
        
        lock_release(of->lock);
        lock_destroy(of->lock);
        kfree(of);
        return 0;
    }

    lock_release(of->lock);
    return 0;
}


int sys_lseek(int fd, off_t pos, int whence, off_t *retval){

    /* Validate process */
    KASSERT(curproc != NULL);

    /* Validate file descriptor */
    if (fd < 0 || fd >= OPEN_MAX) {
        return EBADF;
    }

    /* Get openfile structure */
    struct openfile *of = curproc->fileTable[fd];
    if (of == NULL) {
        return EBADF;
    }

    /* Check if file is seekable */
    if (!VOP_ISSEEKABLE(of->vn)){
        return ESPIPE;
    }

    off_t new_offset;
    int result;
    struct stat statbuf;
    

    /* Acquire lock for thread-safe access */
    lock_acquire(of->lock);

    /* Calculate new offset based on whence */
    switch (whence) {
        case SEEK_SET:
            /* Set to absolute position */
            if (pos < 0) {
                lock_release(of->lock);
                return EINVAL;
            }
            new_offset = pos;
            break;

        case SEEK_CUR:
            /* Set relative to current position */
            /* Check if pos is negative and would make offset negative */
            if (pos < 0 && -pos > of->offset) {
                lock_release(of->lock);
                return EINVAL;
            }
            new_offset = of->offset + pos;
            break;

        case SEEK_END:
            /* Get file size */
            result = VOP_STAT(of->vn, &statbuf);
            if (result) {
                lock_release(of->lock);
                return result;
            }
            /* Check if pos is negative and would make offset negative */
            if (pos < 0 && -pos > statbuf.st_size) {
                lock_release(of->lock);
                return EINVAL;
            }
            new_offset = statbuf.st_size + pos;
            break;

        default:
            /* Invalid whence value */
            lock_release(of->lock);
            return EINVAL;
    }


    /* Check if resulting position is negative */
    if (new_offset < 0) {
        lock_release(of->lock);
        return EINVAL;
    }

    /* Update the file offset */
    of->offset = new_offset;

    /* Return the new position */
    *retval = new_offset;

    lock_release(of->lock);

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
            /* ADD THESE LINES: */
            lock_release(of->lock);
            lock_destroy(of->lock);
            kfree(of);
        } else {
            lock_release(of->lock);
        }
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


int sys_chdir(const char *path){
    
    if (path == NULL) {
        return EFAULT;
    }

    char kbuffer[PATH_MAX];
    size_t len;

    /* copy path from user space */
    int err = copyinstr((const_userptr_t)path, kbuffer, PATH_MAX, &len);
    if (err) {
        return err;
    }

    /* let VFS handle the directory change */
    err = vfs_chdir(kbuffer);
    return err;
}


int sys___getcwd(char *buf, size_t buflen, int *retval){

    if (buf == NULL) {
        return EFAULT;
    }

    /* check if the size of the buffer is valid */
    if (buflen == 0) {
        return EINVAL;
    }

    /* allocate kernel buffer */
    char *kbuf = kmalloc(buflen);
    if (kbuf == NULL) {
        return ENOMEM;
    }

    struct iovec iov;
    struct uio u;

    /* setup uio to write path to kernel buffer */
    uio_kinit(&iov, &u, kbuf, buflen, 0, UIO_READ);

    /* get current working directory from VFS */
    int result = vfs_getcwd(&u);   
    if (result) {
        kfree(kbuf);
        return result;
    }

    /* calculate bytes written */
    size_t len = buflen - u.uio_resid;

    /* copy from kernel buffer to user buffer */
    result = copyout(kbuf, (userptr_t)buf, len);
    if (result) {
        kfree(kbuf);
        return EFAULT;
    }

    /* clean up */
    kfree(kbuf);

    /* return number of bytes written */
    *retval = (int)len;
    return 0;
}

#endif