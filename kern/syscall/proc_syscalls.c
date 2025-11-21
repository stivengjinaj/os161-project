/*
 * AUthor: G.Cabodi
 * Very simple implementation of sys__exit.
 * It just avoids crash/panic. Full process exit still TODO
 * Address space is released
 */

#include <types.h>
#include <kern/unistd.h>
#include <clock.h>
#include <copyinout.h>
#include <syscall.h>
#include <current.h>
#include <lib.h>
#include <thread.h>
#include <addrspace.h>
#include <proc.h>
#include <kern/errno.h>
#include <machine/trapframe.h>
#include <synch.h>
#include <vfs.h>
#include <kern/fcntl.h>
#include <kern/seek.h>
#include <kern/stat.h>
#include <kern/wait.h>
#include <kern/limits.h>

#if OPT_SHELL

/* sys_getpid - Get the process ID of the current process
 *
 *   Returns the process ID of the calling process.
 *
 * Returns:
 *   The process ID of the current process.
 */
pid_t sys_getpid() {
  /* process ID of the current process */
  pid_t result = curproc->p_pid;
  return result;
}

/* sys_fork - Fork a new process
 *
 *   Creates a new process by duplicating the calling process.
 *   The new process (child) gets a copy of the parent's address space
 *   and a copy of the parent's trapframe.
 *
 * Arguments:
 *   tf      - Pointer to the trapframe of the calling process
 *   retval  - Pointer to store the child's process ID in the parent
 *
 * Returns:
 *   0 on success
 *   ENOMEM  - Insufficient memory to create the new process
 */
int sys_fork(struct trapframe *tf, pid_t *retval) {
    struct proc *child_proc;
    struct trapframe *child_tf;
    int result;

    /* ASSERTING CURRENT PROCESS TO ACTUALLY EXIST */
    KASSERT(curproc != NULL);

    /* Create a new process */
    child_proc = proc_create_fork(curproc->p_name);
    if (child_proc == NULL) {
        return ENPROC;  /* No available PID */
    }

    /* Set parent relationship */
    child_proc->parent_pid = curproc->p_pid;

    /* Copy the address space */
    result = as_copy(curproc->p_addrspace, &child_proc->p_addrspace);
    if (result) {
        proc_remove(child_proc->p_pid);
        proc_destroy(child_proc);
        return result; /* Address space copy failed */
    }

    /* Duplicate current working directory */
    spinlock_acquire(&curproc->p_lock);
    if (curproc->p_cwd != NULL) {
        VOP_INCREF(curproc->p_cwd);
        child_proc->p_cwd = curproc->p_cwd;
    }
    spinlock_release(&curproc->p_lock);

    /* Duplicate file table entries */
    for (int i = 0; i < OPEN_MAX; i++) {
        if (curproc->fileTable[i] != NULL) {
            struct openfile *of = curproc->fileTable[i];
            lock_acquire(of->lock);
            of->count++;
            lock_release(of->lock);
            child_proc->fileTable[i] = of;
        }
    }

    /* Copy the trapframe for the child */
    child_tf = kmalloc(sizeof(struct trapframe));
    if (child_tf == NULL) {
        /* Cleanup file table */
        for (int i = 0; i < OPEN_MAX; i++) {
            if (child_proc->fileTable[i] != NULL) {
                lock_acquire(child_proc->fileTable[i]->lock);
                child_proc->fileTable[i]->count--;
                lock_release(child_proc->fileTable[i]->lock);
                child_proc->fileTable[i] = NULL;
            }
        }
        proc_remove(child_proc->p_pid);
        proc_destroy(child_proc);
        return ENOMEM;
    }
    *child_tf = *tf;

    /* Create a new thread for the child process */
    result = thread_fork(
        curthread->t_name,
        child_proc,
        enter_forked_process,
        (void *)child_tf,
        0
    );
    if (result) {
        kfree(child_tf);
        /* Cleanup file table */
        for (int i = 0; i < OPEN_MAX; i++) {
            if (child_proc->fileTable[i] != NULL) {
                lock_acquire(child_proc->fileTable[i]->lock);
                child_proc->fileTable[i]->count--;
                lock_release(child_proc->fileTable[i]->lock);
                child_proc->fileTable[i] = NULL;
            }
        }
        proc_remove(child_proc->p_pid);
        proc_destroy(child_proc);
        return result;
    }

    /* Return the child PID to the parent */
    *retval = child_proc->p_pid;

    return 0;
}

/* sys_execv - Execute a new program
 *
 *   Replaces the current process's address space with a new program
 *   loaded from the specified executable file, and sets up the user stack
 *   with the provided arguments.
 *
 * Arguments:
 *   program - Pointer to the name of the executable file
 *   args    - Pointer to an array of argument strings
 *
 * Returns:
 *   0 on success
 *   EFAULT  - Invalid pointer provided
 *   ENOMEM  - Insufficient memory to load the program
 *   E2BIG   - Argument list too long
 */
int sys_execv(const char *program, char **args) {
    struct vnode *v;
    vaddr_t entrypoint, stackptr;
    struct addrspace *new_as, *old_as;
    char **kernel_args = NULL;
    char *kernel_progname = NULL;
    int argc = 0, result;

    /* Validate input arguments */
    if (!program || !args) {
        return EFAULT;
    }

    /* Copy program name from user space */
    result = copy_program_name(program, &kernel_progname);
    if (result) return result;

    /* Copy arguments from user space */
    result = copy_arguments(args, &kernel_args, &argc);
    if (result) {
        kfree(kernel_progname);
        return result;
    }

    /* Open the executable file */
    result = vfs_open(kernel_progname, O_RDONLY, 0, &v);
    if (result) {
        cleanup_arguments(kernel_args, argc);
        kfree(kernel_progname);
        return result;
    }

    /* Create new address space */
    new_as = as_create();
    if (!new_as) {
        vfs_close(v);
        cleanup_arguments(kernel_args, argc);
        kfree(kernel_progname);
        return ENOMEM;
    }

    /* Switch to new address space */
    old_as = proc_setas(new_as);
    as_activate();

    /* Load the executable */
    result = load_elf(v, &entrypoint);
    if (result) {
        restore_old_address_space(old_as, new_as);
        vfs_close(v);
        cleanup_arguments(kernel_args, argc);
        kfree(kernel_progname);
        return result;
    }
    vfs_close(v);

    /* Define user stack */
    result = as_define_stack(new_as, &stackptr);
    if (result) {
        restore_old_address_space(old_as, new_as);
        cleanup_arguments(kernel_args, argc);
        kfree(kernel_progname);
        return result;
    }

    /* Copy arguments to user stack */
    result = copy_args_to_stack(kernel_args, argc, &stackptr);
    if (result) {
        restore_old_address_space(old_as, new_as);
        cleanup_arguments(kernel_args, argc);
        kfree(kernel_progname);
        return result;
    }

    /* Cleanup before executing */
    cleanup_arguments(kernel_args, argc);
    kfree(kernel_progname);

    /* Execute new process */
    enter_new_process(argc, (userptr_t)stackptr, NULL, stackptr, entrypoint);

    panic("enter_new_process returned unexpectedly!");
    return EINVAL;
}

/* sys_waitpid - Wait for a specific child process to exit
 *
 *   Waits for the child process with the specified PID to exit and retrieves
 *   its exit status.
 *
 * Arguments:
 *   pid     - Process ID of the child to wait for
 *   status  - Pointer to store the exit status of the child
 *   options - Options for waiting (currently only WNOHANG is supported)
 *   retval  - Pointer to store the PID of the exited child
 *
 * Returns:
 *   0 on success
 *   EINVAL  - Invalid options provided
 *   ECHILD  - Specified PID is not a child of the calling process
 *   ESRCH   - No such process with the specified PID
 *   EFAULT  - Invalid status pointer provided
 */
int sys_waitpid(pid_t pid, int *status, int options, int *retval) {
     /* 1. Check for invalid options */
    if (options != 0)
        return EINVAL;

    /* 2. Validate pid range */
    if (pid <= 0 || pid > PROC_MAX)
        return ESRCH;

    /* 3. Retrieve child process */
    struct proc *child = proc_search(pid);
    if (child == NULL)
        return ESRCH;

    /* 4. Ensure the caller is the parent of this process */
    if (child->parent_pid != curproc->p_pid)
        return ECHILD;

    /* 5. Wait until the child calls _exit() */
    lock_acquire(child->p_locklock);
    while (!child->p_exited) {
        cv_wait(child->p_cv, child->p_locklock);
    }
    int exitcode = child->p_exitcode;
    
    /* 6. Wait until child thread count is 0 (child has fully exited) */
    while (child->p_numthreads > 0) {
        lock_release(child->p_locklock);
        thread_yield(); /* Give child thread time to fully exit */
        lock_acquire(child->p_locklock);
    }
    lock_release(child->p_locklock);

    /* 7. Copy exit status to user space, if requested */
    if (status != NULL) {
        int result = copyout(&exitcode, (userptr_t)status, sizeof(int));
        if (result)
            return result;
    }

    /* 8. Now safe to remove from process table and destroy */
    proc_remove(pid);
    
    /* Destroy synchronization primitives */
    if (child->p_cv != NULL) {
        cv_destroy(child->p_cv);
        child->p_cv = NULL;
    }
    if (child->p_locklock != NULL) {
        lock_destroy(child->p_locklock);
        child->p_locklock = NULL;
    }
    
    spinlock_cleanup(&child->p_lock);
    kfree(child->p_name);
    kfree(child);

    /* 9. Return child's pid */
    *retval = pid;
    return 0;
}

/* sys__exit - Exit the current process
 *
 *   Terminates the calling process with the specified exit code.
 *   Cleans up resources and notifies the parent process if necessary.
 *
 * Arguments:
 *   exitcode - Exit code of the process
 */
void sys__exit(int exitcode) {
    struct proc *p = curproc;
    
    KASSERT(p != NULL);
    
    /* Clean up process resources (address space, files, etc.) */
    
    /* VFS fields */
    if (p->p_cwd) {
        VOP_DECREF(p->p_cwd);
        p->p_cwd = NULL;
    }

    /* VM fields - must happen before proc_remthread */
    if (p->p_addrspace) {
        struct addrspace *as;
        as = proc_setas(NULL);
        as_deactivate();
        as_destroy(as);
    }

    /* Close all open files */
    for(int i = 0; i < OPEN_MAX; i++) {
        if (p->fileTable[i] != NULL) {
            struct openfile *of = p->fileTable[i];
            
            lock_acquire(of->lock);
            of->count--;

            if (of->count == 0) {
                vfs_close(of->vn);
                lock_release(of->lock);
                lock_destroy(of->lock);
                kfree(of);
            } else {
                lock_release(of->lock);
            }

            p->fileTable[i] = NULL;
        }
    }
    
    /* Mark as exited and wake parent - BEFORE removing thread */
    lock_acquire(p->p_locklock);
    p->p_exitcode = _MKWAIT_EXIT(exitcode);
    p->p_exited = true;
    cv_signal(p->p_cv, p->p_locklock);
    lock_release(p->p_locklock);
    
    /* Remove thread from process */
    proc_remthread(curthread);
    
    /* Thread exits */
    thread_exit();
    
    panic("thread_exit returned (should not happen)\n");
}

#endif