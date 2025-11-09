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
int
sys_fork(struct trapframe *tf, pid_t *retval)
{
    struct proc *parent = curproc;
    struct proc *child = NULL;
    struct trapframe *child_tf = NULL;
    int result = 0;

    /* Sanity checks */
    if (tf == NULL || retval == NULL) {
        return EINVAL;
    }

    /*
     * 1) Create a new process structure.
     *    proc_create() in your tree already assigns a fresh PID and inserts
     *    into the process table; it initializes p_cv, p_locklock, etc.
     */
    child = proc_create_runprogram(parent->p_name);
    if (child == NULL) {
        return ENPROC; /* or ENOMEM; ENPROC is acceptable for "no more procs" */
    }

    /* Set parent relationship (used by waitpid / ECHILD checks). */
    child->parent_pid = parent->p_pid;

    /*
     * 2) Copy address space.
     *    Each process must have an independent addrspace.
     */
    KASSERT(parent->p_addrspace != NULL);
    result = as_copy(parent->p_addrspace, &child->p_addrspace);
    if (result) {
        proc_destroy(child);
        return result; /* typically ENOMEM */
    }

    /*
     * 3) Duplicate current working directory (bump refcount).
     */
    spinlock_acquire(&parent->p_lock);
    if (parent->p_cwd != NULL) {
        VOP_INCREF(parent->p_cwd);
        child->p_cwd = parent->p_cwd;
    }
    spinlock_release(&parent->p_lock);

    /*
     * 4) Duplicate file table entries:
     *    Share openfile objects; increment their refcount under the lock.
     *    This matches the fork semantics: tables are independent, objects shared.
     */
    for (int i = 0; i < OPEN_MAX; i++) {
        struct openfile *of = parent->fileTable[i];
        if (of != NULL) {
            /* Share pointer and bump reference count safely */
            lock_acquire(of->lock);
            of->count++;
            lock_release(of->lock);
            child->fileTable[i] = of;
        } else {
            child->fileTable[i] = NULL;
        }
    }

    /*
     * 5) Snapshot trapframe for the child.
     *    We allocate a kernel copy that enter_forked_process will consume.
     */
    child_tf = kmalloc(sizeof(struct trapframe));
    if (child_tf == NULL) {
        proc_destroy(child);
        return ENOMEM;
    }
    *child_tf = *tf; /* struct copy */

    /*
     * 6) Create the child thread.
     *    The child thread will:
     *      - activate its address space,
     *      - set v0=0, a3=0, advance epc,
     *      - enter usermode via mips_usermode() in enter_forked_process.
     */
    result = thread_fork(
        parent->p_name,          /* thread name (debug) */
        child,                   /* new thread's process */
        enter_forked_process,    /* entry function (your version taking void*) */
        (void *)child_tf,        /* data (the trapframe copy) */
        0                        /* unused */
    );
    if (result) {
        /* thread_fork did not consume child_tf on failure */
        kfree(child_tf);
        proc_destroy(child);
        return result; /* usually ENOMEM */
    }

    /*
     * 7) Parent’s return value is the child's pid.
     *    Child returns 0 from user mode (handled in enter_forked_process).
     */
    *retval = child->p_pid;
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

    return exec_sys_execv(program, args);

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
    lock_release(child->p_locklock);

    /* 6. Copy exit status to user space, if requested */
    if (status != NULL) {
        int result = copyout(&exitcode, (userptr_t)status, sizeof(int));
        if (result)
            return result;
    }

    /* 7. Destroy the child process (reap it) */
    proc_destroy(child);

    /* 8. Return child's pid */
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

    /* Remove thread from process FIRST (before signaling parent) */
    proc_remthread(curthread);
    
    /* Store exit code */
    lock_acquire(p->p_locklock);
    p->p_exitcode = _MKWAIT_EXIT(exitcode);
    p->p_exited = true;
    
    /* Wake up parent if it's waiting */
    cv_signal(p->p_cv, p->p_locklock);

    /* Keep lock held until thread_exit to prevent parent from destroying
     * the process structure while we're still using it */
    lock_release(p->p_locklock);
    
    
    /* Thread exits */
    thread_exit();
    
    panic("thread_exit returned (should not happen)\n");
}

#endif